#include "SignalChain.h"

namespace namvx
{
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
    monoF.assign ((size_t) maxBlock, 0.0f);
    stereo.setSize (2, maxBlock);

    for (auto* s : { &inGain, &residualIn, &residualOut, &volumeGain, &masterGain, &outGain, &tubeMix, &tubeDrive, &paDrive })
        s->prepare (sr, 25.0);
    inGain.snap (1.0); residualIn.snap (1.0); residualOut.snap (1.0); volumeGain.snap (1.0);
    masterGain.snap (1.0); outGain.snap (1.0); tubeMix.snap (0.0); tubeDrive.snap (0.3); paDrive.snap (0.5);

    residual.prepare (sr, maxBlock);
    toneMatch.prepare (sr);
    cabinet.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    ampFadeLen = (int) (0.06 * sr);
    chanFadeLen = (int) (0.035 * sr);
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
        chanFade = chanFadeLen;
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
        res = rt->setKnobs (channel, p.knobs);
        if (chanFade > 0 && prevChannel >= 0) rt->setKnobs (prevChannel, p.knobs);
        if (rt->isBlend()) rt->setBlendLevels (channel, p.blendMix);
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
        if (chanFade > 0 && prevChannel >= 0)
        {
            rt->process (prevChannel, scratch.data(), ampTmp.data(), n);
            for (int i = 0; i < n; ++i)
            {
                const double g = (double) chanFade / chanFadeLen;
                ampOut[(size_t) i] = ampOut[(size_t) i] * (1.0 - g) + ampTmp[(size_t) i] * g;
                if (chanFade > 0) --chanFade;
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
            const double g = (double) ampFade / ampFadeLen;
            ampOut[(size_t) i] = ampOut[(size_t) i] * (1.0 - g) + ampTmp[(size_t) i] * g;
            if (ampFade > 0) --ampFade;
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
        corr = rt->toneCorrection (channel, p.blendMix, p.toneSource);
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

    // ---- output gain + preset dip ------------------------------------------------------------
    if (dipRequested.exchange (false)) { dipPhase = 1; dipCounter = 0; }
    outGain.setTarget (dbToGain (p.outputDb));
    const int dipLen = (int) (0.012 * sampleRate);
    float opk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (dipPhase == 1) { dipEnv = std::max (0.0, dipEnv - 1.0 / dipLen); if (dipEnv <= 0.0) { dipPhase = 2; dipCounter = dipLen; } }
        else if (dipPhase == 2) { if (--dipCounter <= 0) dipPhase = 3; }
        else if (dipPhase == 3) { dipEnv = std::min (1.0, dipEnv + 1.0 / dipLen); if (dipEnv >= 1.0) dipPhase = 0; }
        const float gg = (float) (outGain.next() * dipEnv);
        const float l = stereo.getSample (0, i) * gg, r = stereo.getSample (1, i) * gg;
        stereo.setSample (0, i, std::isfinite (l) ? l : 0.0f);
        stereo.setSample (1, i, std::isfinite (r) ? r : 0.0f);
        opk = std::max (opk, std::max (std::abs (l), std::abs (r)));
    }
    outputPeak = std::max (opk, outputPeak.load() * 0.9f);
}
} // namespace namvx
