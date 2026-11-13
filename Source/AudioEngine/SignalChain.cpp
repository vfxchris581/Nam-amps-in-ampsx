#include "SignalChain.h"

namespace namknobs
{
static_assert (kBlendSlots == AmpDefinition::kMaxBlendModels, "slot counts must match");

SignalChain::SignalChain() = default;
SignalChain::~SignalChain() = default;

void SignalChain::prepare (double sr, int block)
{
    sampleRate = sr;
    maxBlock = std::max (1, block);
    mono.assign ((size_t) maxBlock, 0.0);
    ampOut.assign ((size_t) maxBlock, 0.0);
    ampTmp.assign ((size_t) maxBlock, 0.0);
    scratch.assign ((size_t) maxBlock, 0.0);
    gateBuf.assign ((size_t) maxBlock, 1.0);
    gate.prepare (sr);
    monoF.assign ((size_t) maxBlock, 0.0f);
    stereo.setSize (2, maxBlock);

    for (auto* s : { &inGain, &residualIn, &residualOut, &volumeGain, &masterGain, &outGain, &tubeMix, &tubeDrive, &paDrive })
        s->prepare (sr, 25.0);
    inGain.snap (1.0); residualIn.snap (1.0); residualOut.snap (1.0); volumeGain.snap (1.0);
    masterGain.snap (1.0); outGain.snap (1.0); tubeMix.snap (0.0); tubeDrive.snap (0.3); paDrive.snap (0.5);

    residual.prepare (sr, maxBlock);
    toneMatch.prepare (sr);
    eq.prepare (sr);
    cabinet.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    ampFadeLen = (int) (0.06 * sr);
    chanFadeLen = (int) (0.035 * sr);
    chanWarmLen = (int) (0.025 * sr); // the new channel's captures settle before they are heard
    setOversampling (osQuality, osLinearPhase);
}

void SignalChain::setOversampling (OversamplingQuality q, bool linearPhase)
{
    osQuality = q;
    osLinearPhase = linearPhase;
    const auto type = linearPhase ? juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple
                                  : juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR;
    osPre = std::make_unique<juce::dsp::Oversampling<double>> (1, (size_t) q, type, true, true);
    osPost = std::make_unique<juce::dsp::Oversampling<double>> (1, (size_t) q, type, true, true);
    osPre->initProcessing ((size_t) maxBlock);
    osPost->initProcessing ((size_t) maxBlock);
    const double osRate = sampleRate * (double) (1 << (int) q);
    triode.prepare (osRate);
    powerAmp.prepare (osRate);
    harmonics.prepare (osRate);
    updateLatency();
}

void SignalChain::updateLatency() noexcept
{
    const int os = osPre ? (int) std::lround (osPre->getLatencyInSamples() + osPost->getLatencyInSamples()) : 0;
    latency = os + runtimeLatency;
}

void SignalChain::publishRuntime (std::unique_ptr<AmpRuntime> rt)
{
    runtimes.publish (std::move (rt));
}

void SignalChain::process (double* const* io, int numIn, int numOut, int n) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);
        double* buf = mono.data();
        if (numIn > 0) std::copy (io[0] + start, io[0] + start + len, buf);
        else std::fill (buf, buf + len, 0.0);
        processMono (buf, len);
        for (int c = 0; c < numOut; ++c)
        {
            const float* src = stereo.getReadPointer (numOut == 1 ? 0 : std::min (c, 1));
            for (int i = 0; i < len; ++i)
                io[c][start + i] = numOut == 1 ? 0.5 * ((double) stereo.getSample (0, i) + stereo.getSample (1, i)) : (double) src[i];
        }
    }
}

void SignalChain::process (float* const* io, int numIn, int numOut, int n) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);
        double* buf = mono.data();
        if (numIn > 0) for (int i = 0; i < len; ++i) buf[i] = (double) io[0][start + i];
        else std::fill (buf, buf + len, 0.0);
        processMono (buf, len);
        for (int c = 0; c < numOut; ++c)
        {
            if (numOut == 1)
                for (int i = 0; i < len; ++i) io[c][start + i] = 0.5f * (stereo.getSample (0, i) + stereo.getSample (1, i));
            else
                std::copy (stereo.getReadPointer (std::min (c, 1)), stereo.getReadPointer (std::min (c, 1)) + len, io[c] + start);
        }
    }
}

void SignalChain::processMono (double* x, int n) noexcept
{
    const auto& p = params;

    // ---- amp hand-over -------------------------------------------------------------------
    AmpRuntime* previous = nullptr;
    AmpRuntime* rt = runtimes.acquire (previous);
    if (previous != nullptr || (rt != nullptr && runtimeLatency != rt->getLatency()))
    {
        if (fadingOut != nullptr) runtimes.retire (fadingOut); // a fade was still running: drop it
        fadingOut = previous;
        ampFade = previous != nullptr ? ampFadeLen : 0;
        ampWarm = previous != nullptr ? chanWarmLen : 0; // new amp settles silently first
        runtimeLatency = rt != nullptr ? rt->getLatency() : 0;
        updateLatency();
    }
    hasAmp = rt != nullptr;
    currentRuntimeChannels = rt != nullptr ? rt->numChannels() : 0;

    // ---- channel switching -----------------------------------------------------------------
    const int channel = rt != nullptr ? std::clamp (p.channel, 0, std::max (0, rt->numChannels() - 1)) : 0;
    if (channel != lastChannel)
    {
        prevChannel = lastChannel;
        lastChannel = channel;
        chanWarm = chanWarmLen;
        chanFade = chanFadeLen;
    }

    // ---- tuner feed (raw guitar, before any gain) ---------------------------------------------
    if (tunerActive.load (std::memory_order_relaxed))
    {
        int s1, n1, s2, n2;
        tunerFifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) tunerBuf[(size_t) (s1 + i)] = (float) x[i];
        for (int i = 0; i < n2; ++i) tunerBuf[(size_t) (s2 + i)] = (float) x[n1 + i];
        tunerFifo.finishedWrite (n1 + n2);
    }

    // ---- input + metering --------------------------------------------------------------------
    inGain.setTarget (dbToGain (p.inputDb));
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        x[i] *= inGain.next();
        pk = std::max (pk, (float) std::abs (x[i]));
    }
    inputPeak = std::max (pk, inputPeak.load() * 0.9f);

    // ---- noise gate detector: listens to the guitar before the amp -----------------------------
    gate.setParams (p.gateOn, p.gateThresholdDb, p.gateReleaseMs);
    gate.computeGain (x, gateBuf.data(), n);
    gateGain = (float) gate.currentGain();

    // ---- 12AX7 pre stage (oversampled; always runs for constant latency) ---------------------
    {
        tubeMix.setTarget (p.preTubeOn ? p.preTubeMix : 0.0);
        tubeDrive.setTarget (p.preTubeDrive);
        juce::dsp::AudioBlock<double> block (&x, 1, (size_t) n);
        auto up = osPre->processSamplesUp (block);
        auto* u = up.getChannelPointer (0);
        const int un = (int) up.getNumSamples();
        const int ratio = std::max (1, un / std::max (1, n));
        if (tubeMix.getCurrent() > 0.0 || tubeMix.getTarget() > 0.0)
        {
            for (int i = 0; i < un; ++i)
            {
                if (i % ratio == 0) { tubeMix.next(); triode.setDrive (tubeDrive.next()); }
                const double m = tubeMix.getCurrent();
                u[i] = (1.0 - m) * u[i] + m * triode.process (u[i]);
            }
        }
        osPre->processSamplesDown (block);
    }

    // ---- NAM capture blend ------------------------------------------------------------------
    ResidualAmounts res;
    if (rt != nullptr)
    {
        rt->setPhaseMode ((PhaseMode) p.phaseMode);
        rt->setLevelMatch (p.levelMatch);
        if (rt->isBlend())
            for (int s = 0; s < kBlendSlots; ++s) slotMatchDb[(size_t) s] = (float) rt->matchDbForSlot (channel, s);
        res = rt->setKnobs (channel, p.knobs);
        if ((chanWarm > 0 || chanFade > 0) && prevChannel >= 0) rt->setKnobs (prevChannel, p.knobs);
        if (rt->isBlend())
        {
            // Effective tone matrix: manual knobs, or derived from the amp knobs in Auto mode.
            effGain = p.blendMix;
            effBands = p.toneSource;
            if (p.matrixAuto)
            {
                const auto ab = rt->autoBlend (channel, p.knobs);
                effGain = ab.gain;
                effBands = ab.bands;
                // Real-amp knob law: every knob always spans a full amp range around noon.
                // The captures supply the character; the DSP tone stack supplies whatever range
                // the captures don't (desired = (knob-5) units, minus what the transfer achieved).
                const auto corr = rt->toneCorrection (channel, effGain, effBands);
                const auto& kn = p.knobs;
                res.resonance = (kn[(size_t) Control::Resonance] - 5.0) - corr[0] / ToneResidual::kResonanceDb;
                res.bass      = (kn[(size_t) Control::Bass] - 5.0)      - corr[1] / ToneResidual::kBassDb;
                res.middle    = (kn[(size_t) Control::Middle] - 5.0)    - corr[2] / ToneResidual::kMidDb;
                res.treble    = (kn[(size_t) Control::Treble] - 5.0)    - corr[3] / ToneResidual::kTrebleDb;
                res.presence  = (kn[(size_t) Control::Presence] - 5.0)  - corr[4] / ToneResidual::kPresenceDb;
                // Gain: crossfade clean -> dirty captures AND push their input like a preamp gain pot.
                res.gain = (kn[(size_t) Control::Gain] - 5.0) * (ab.varies[0] ? 0.55 : 1.0);
            }
            rt->setBlendLevels (channel, effGain);
            rt->setSlotPolarity (channel, p.slotInvert);
            for (int m = 0; m < kBlendSlots; ++m)
            {
                matrixWeights[0][(size_t) m] = (float) effGain[(size_t) m];
                for (int b = 0; b < 5; ++b) matrixWeights[(size_t) b + 1][(size_t) m] = (float) effBands[(size_t) b][(size_t) m];
            }
        }
    }
    else
    {
        // no amp loaded: knobs still act through the residual stage around a neutral 5
        res = { p.knobs[0] - 5.0, p.knobs[2] - 5.0, p.knobs[3] - 5.0, p.knobs[4] - 5.0, p.knobs[5] - 5.0, p.knobs[6] - 5.0 };
    }

    residualIn.setTarget (ToneResidual::inputGainFor (res.gain));
    residualOut.setTarget (ToneResidual::makeupFor (res.gain));
    for (int i = 0; i < n; ++i) scratch[(size_t) i] = x[i] * residualIn.next();

    if (rt != nullptr)
    {
        rt->process (channel, scratch.data(), ampOut.data(), n);
        if ((chanWarm > 0 || chanFade > 0) && prevChannel >= 0)
        {
            rt->process (prevChannel, scratch.data(), ampTmp.data(), n);
            for (int i = 0; i < n; ++i)
            {
                double g = 1.0; // warm-up: the new channel runs silently, the old one keeps playing
                if (chanWarm > 0) --chanWarm;
                else if (chanFade > 0) { g = (double) chanFade / chanFadeLen; --chanFade; }
                else g = 0.0;
                ampOut[(size_t) i] = ampOut[(size_t) i] * (1.0 - g) + ampTmp[(size_t) i] * g;
            }
        }
    }
    else
        std::copy (scratch.begin(), scratch.begin() + n, ampOut.begin());

    if (fadingOut != nullptr)
    {
        fadingOut->setKnobs (std::min (channel, fadingOut->numChannels() - 1), p.knobs);
        if (fadingOut->isBlend()) fadingOut->setBlendLevels (std::min (channel, fadingOut->numChannels() - 1), p.blendMix);
        fadingOut->process (std::min (channel, fadingOut->numChannels() - 1), scratch.data(), ampTmp.data(), n);
        for (int i = 0; i < n; ++i)
        {
            double g = 1.0;
            if (ampWarm > 0) --ampWarm;
            else { g = (double) ampFade / ampFadeLen; if (ampFade > 0) --ampFade; }
            ampOut[(size_t) i] = ampOut[(size_t) i] * (1.0 - g) + ampTmp[(size_t) i] * g;
        }
        if (ampFade <= 0) { runtimes.retire (fadingOut); fadingOut = nullptr; }
    }
    runningModels = rt != nullptr ? rt->numRunningModels (channel) : 0;

    // ---- make-up, volume, residual tone -------------------------------------------------------
    volumeGain.setTarget (ToneResidual::levelTaper (p.knobs[(size_t) Control::Volume]));
    for (int i = 0; i < n; ++i) x[i] = ampOut[(size_t) i] * residualOut.next() * volumeGain.next();

    residual.setAmounts (res, p.powerAmpOn);
    residual.process (x, n);

    // ---- blend tone transfer: each band takes the balance of its chosen source captures --------
    ToneMatch::Bands corr {};
    if (rt != nullptr && rt->isBlend())
        corr = rt->toneCorrection (channel, effGain, effBands);
    for (int b = 0; b < 5; ++b) corr[(size_t) b] += p.toneTrimDb[(size_t) b]; // fine-tune knobs
    for (int b = 0; b < 5; ++b) toneCorrectionDb[(size_t) b] = (float) corr[(size_t) b];
    toneMatch.setCorrectionDb (corr);
    toneMatch.process (x, n);

    // ---- harmonic engine + power amp (oversampled) -------------------------------------------
    {
        const double master = p.knobs[(size_t) Control::Master];
        masterGain.setTarget (ToneResidual::levelTaper (master));
        paDrive.setTarget (master / 10.0);

        harmonics.setSettings (p.harm);
        powerAmp.setTubeType ((PowerAmpStage::TubeType) p.tubeType);
        powerAmp.setBias (p.bias);
        powerAmp.setSag (p.sag);
        powerAmp.setFeedback (p.nfb);
        powerAmp.setTransformer (p.transformer);
        powerAmp.setMismatch (p.mismatch);
        powerAmp.setSpeakerInteraction (p.speaker);
        powerAmp.setPresenceResonance (res.presence * ToneResidual::kPresenceDb, res.resonance * ToneResidual::kResonanceDb);

        juce::dsp::AudioBlock<double> block (&x, 1, (size_t) n);
        auto up = osPost->processSamplesUp (block);
        auto* u = up.getChannelPointer (0);
        const int un = (int) up.getNumSamples();
        const int ratio = std::max (1, un / std::max (1, n));
        for (int i = 0; i < un; ++i)
        {
            double v = u[i];
            if (p.harmonicsOn) v = harmonics.process (v);
            if (p.powerAmpOn)
            {
                if (i % ratio == 0) powerAmp.setDrive (paDrive.next());
                v = powerAmp.process (v);
            }
            u[i] = v;
        }
        osPost->processSamplesDown (block);
        for (int i = 0; i < n; ++i) x[i] *= masterGain.next();
    }

    // ---- noise gate gain: after the amp, so its amplified hiss goes too; the cab tail rings out
    for (int i = 0; i < n; ++i) x[i] *= gateBuf[(size_t) i];

    // ---- studio EQ (before the cabinet) -------------------------------------------------------
    eq.setSettings (p.eq);
    if (! p.eq.postCab) eq.process (x, n);

    // ---- cabinet -----------------------------------------------------------------------------
    DualCabinet::Global g;
    g.enabled = p.cabOn; g.stereo = p.cabStereo; g.autoAlign = p.cabAutoAlign;
    g.blend = (float) p.cabBlend; g.levelDb = (float) p.cabLevelDb;
    cabinet.setGlobal (g);
    auto toSlot = [] (const SlotParams& s) {
        IRSlot::Params q; q.enabled = s.enabled; q.levelDb = (float) s.levelDb; q.pan = (float) s.pan;
        q.invert = s.invert; q.delayMs = (float) s.delayMs; return q; };
    cabinet.setSlotParams (0, toSlot (p.ir1));
    cabinet.setSlotParams (1, toSlot (p.ir2));
    for (int i = 0; i < n; ++i) monoF[(size_t) i] = (float) x[i];
    cabinet.process (monoF.data(), stereo, n);

    // ---- studio EQ (after the cabinet) --------------------------------------------------------
    if (p.eq.postCab) eq.process (stereo.getWritePointer (0), stereo.getWritePointer (1), n);

    // ---- output gain + preset dip ------------------------------------------------------------
    if (dipRequested.exchange (false)) { dipPhase = 1; dipCounter = 0; }
    outGain.setTarget (dbToGain (p.outputDb));
    const int dipLen = (int) (0.012 * sampleRate);
    const double muteTarget = (tunerActive.load (std::memory_order_relaxed) && tunerMute.load (std::memory_order_relaxed)) ? 0.0 : 1.0;
    const double muteStep = 1.0 / (0.010 * sampleRate);
    float opk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (dipPhase == 1) { dipEnv = std::max (0.0, dipEnv - 1.0 / dipLen); if (dipEnv <= 0.0) { dipPhase = 2; dipCounter = dipLen; } }
        else if (dipPhase == 2) { if (--dipCounter <= 0) dipPhase = 3; }
        else if (dipPhase == 3) { dipEnv = std::min (1.0, dipEnv + 1.0 / dipLen); if (dipEnv >= 1.0) dipPhase = 0; }
        muteEnv = muteTarget > muteEnv ? std::min (muteTarget, muteEnv + muteStep) : std::max (muteTarget, muteEnv - muteStep);
        const float gg = (float) (outGain.next() * dipEnv * muteEnv);
        const float l = stereo.getSample (0, i) * gg, r = stereo.getSample (1, i) * gg;
        stereo.setSample (0, i, std::isfinite (l) ? l : 0.0f);
        stereo.setSample (1, i, std::isfinite (r) ? r : 0.0f);
        opk = std::max (opk, std::max (std::abs (l), std::abs (r)));
    }
    outputPeak = std::max (opk, outputPeak.load() * 0.9f);

    // feed the EQ page's spectrum analyser (drops samples if the UI isn't reading)
    int s1, n1, s2, n2;
    spectrumFifo.prepareToWrite (n, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) spectrumBuf[(size_t) (s1 + i)] = 0.5f * (stereo.getSample (0, i) + stereo.getSample (1, i));
    for (int i = 0; i < n2; ++i) spectrumBuf[(size_t) (s2 + i)] = 0.5f * (stereo.getSample (0, n1 + i) + stereo.getSample (1, n1 + i));
    spectrumFifo.finishedWrite (n1 + n2);
}

int SignalChain::readTunerSamples (float* dest, int maxSamples) noexcept
{
    int s1, n1, s2, n2;
    tunerFifo.prepareToRead (std::min (maxSamples, tunerFifo.getNumReady()), s1, n1, s2, n2);
    std::copy (tunerBuf.begin() + s1, tunerBuf.begin() + s1 + n1, dest);
    std::copy (tunerBuf.begin() + s2, tunerBuf.begin() + s2 + n2, dest + n1);
    tunerFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

int SignalChain::readSpectrumSamples (float* dest, int maxSamples) noexcept
{
    int s1, n1, s2, n2;
    spectrumFifo.prepareToRead (std::min (maxSamples, spectrumFifo.getNumReady()), s1, n1, s2, n2);
    std::copy (spectrumBuf.begin() + s1, spectrumBuf.begin() + s1 + n1, dest);
    std::copy (spectrumBuf.begin() + s2, spectrumBuf.begin() + s2 + n2, dest + n1);
    spectrumFifo.finishedRead (n1 + n2);
    return n1 + n2;
}
} // namespace namknobs
