#include "AmpRuntime.h"

#include <algorithm>
#include "ProfileAnalyzer/ProfileAnalyzer.h"
#include "ProfileInterpolation/PhaseMatcher.h"
#include "LevelMatch.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

namespace namknobs
{
std::unique_ptr<AmpRuntime> AmpRuntime::build (const AmpDefinition& def, double sr, int maxBlock,
                                               const RuntimeOptions& opts, std::vector<std::string>& errors)
{
    auto rt = std::make_unique<AmpRuntime>();
    rt->ampId = def.id;
    rt->blend = def.isBlend;
    rt->channels.resize (def.channels.size());

    // ---- 1. Load and measure every capture in parallel ------------------------------------------
    // Each capture is played one continuous test signal: a guitar phrase (level match), a short
    // gap, then the phase probe (phase matching), then flushed. Captures are independent, so this
    // runs on several cores: an 18-capture blend is ready in about the time of a couple of captures.
    struct Prep
    {
        const CaptureEntry* cap = nullptr;
        std::unique_ptr<NamModel> model;
        std::string error;
        double loudnessDb = -200.0;
        std::vector<double> probeOut; // response from the start of the probe (+ tail)
    };
    std::vector<std::vector<Prep>> prep (def.channels.size());
    std::vector<Prep*> jobs;
    for (size_t ci = 0; ci < def.channels.size(); ++ci)
    {
        for (const auto& cap : def.channels[ci].captures)
            if (cap.enabled) { prep[ci].emplace_back(); prep[ci].back().cap = &cap; }
        for (auto& pr : prep[ci]) jobs.push_back (&pr);
    }
    const auto phrase = LevelMatch::testPhrase (sr);
    const auto probe = PhaseMatcher::makeProbe (sr);
    const size_t gap = (size_t) (0.1 * sr), tail = (size_t) (0.1 * sr);
    const size_t probeStart = phrase.size() + gap;
    std::vector<double> testSignal (probeStart + probe.size() + tail, 0.0);
    std::copy (phrase.begin(), phrase.end(), testSignal.begin());
    std::copy (probe.begin(), probe.end(), testSignal.begin() + (long) probeStart);

    auto work = [&] (Prep& pr) {
        try
        {
            auto m = std::make_unique<NamModel>();
            m->load (pr.cap->path);
            m->prepare (sr, maxBlock);
            std::vector<double> y (testSignal.size()), zeros ((size_t) maxBlock, 0.0), buf ((size_t) maxBlock);
            for (size_t p = 0; p < testSignal.size(); p += (size_t) maxBlock)
            {
                const int n = (int) std::min ((size_t) maxBlock, testSignal.size() - p);
                m->process (testSignal.data() + p, y.data() + p, n);
            }
            pr.loudnessDb = LevelMatch::loudnessDb (std::vector<double> (y.begin(), y.begin() + (long) phrase.size()), sr);
            pr.probeOut.assign (y.begin() + (long) probeStart, y.end());
            // flush the test signal out of the network's memory before real audio starts
            for (int f = 0; f < (int) (0.2 * sr) / maxBlock + 1; ++f) m->process (zeros.data(), buf.data(), maxBlock);
            pr.model = std::move (m);
        }
        catch (const std::exception& e) { pr.error = e.what(); }
        catch (...) { pr.error = "could not load " + pr.cap->path; }
    };
    {
        const int hw = (int) std::thread::hardware_concurrency();
        const int nThreads = std::clamp (hw > 1 ? hw - 1 : 1, 1, std::min (8, std::max (1, (int) jobs.size())));
        std::atomic<size_t> next { 0 };
        auto runner = [&] { for (size_t j; (j = next.fetch_add (1)) < jobs.size();) work (*jobs[j]); };
        std::vector<std::thread> pool;
        for (int t = 1; t < nThreads; ++t) pool.emplace_back (runner);
        runner();
        for (auto& t : pool) t.join();
    }

    // ---- 2. Assemble each channel ---------------------------------------------------------------
    for (size_t ci = 0; ci < def.channels.size(); ++ci)
    {
        const auto& chDef = def.channels[ci];
        auto& ch = rt->channels[ci];
        std::vector<KnobCoords> coords;
        std::vector<bool> enabled;
        std::vector<const CaptureEntry*> used;
        std::vector<Prep*> usedPrep;
        (void) chDef;

        for (auto& pr : prep[ci])
        {
            const auto& cap = *pr.cap;
            if (pr.model == nullptr) { errors.push_back (pr.error); continue; }
            {
                ch.models.push_back (std::move (pr.model));
                std::array<double, 5> bd {};
                if (cap.analysis && cap.analysis->valid)
                    for (int b = 0; b < 5; ++b) bd[(size_t) b] = cap.analysis->bandsDb[(size_t) b];
                ch.info.bands.push_back (bd);
                ch.info.gainFeature.push_back (cap.analysis && cap.analysis->valid ? ProfileAnalyzer::featureFor (Control::Gain, *cap.analysis) : 0.0);
                ch.info.slot.push_back (std::clamp (cap.blendSlot >= 0 ? cap.blendSlot : (int) ch.info.slot.size(), 0, AmpDefinition::kMaxBlendModels - 1));
                coords.push_back (cap.coords);
                enabled.push_back (true);
                used.push_back (&cap);
                usedPrep.push_back (&pr);
            }
        }

        if (def.isBlend)
            for (auto& k : coords) k = unknownCoords(); // blends: no knob mapping, every knob is DSP
        ch.interp.setCaptures (coords, enabled);
        ch.interp.setMaxActive (opts.maxActiveCaptures);

        // Baselines for controls no capture varies: the single known value, else neutral 5.
        for (int d = 0; d < kNumControls; ++d)
        {
            double v = 5.0;
            for (auto& k : coords) if (isKnown (k[(size_t) d])) { v = k[(size_t) d]; break; }
            ch.baseline[(size_t) d] = v;
        }

        // Loudness: metadata loudness if every capture has it, else analysis loudness.
        const bool allMeta = ! used.empty() && std::all_of (used.begin(), used.end(), [] (auto* c) { return c->meta.loudnessDb.has_value(); });
        int refPolarity = 0;
        std::vector<BlendSource> sources;
        for (size_t i = 0; i < used.size(); ++i)
        {
            const auto* cap = used[i];
            BlendSource s;
            s.model = ch.models[i].get();
            double loud = opts.referenceLoudnessDb;
            if (allMeta) loud = *cap->meta.loudnessDb;
            else if (cap->analysis && cap->analysis->valid) loud = cap->analysis->loudnessDb;
            s.staticGain = dbToGain (cap->trimDb);
            // fallback if the capture can't be measured below: stored / analysed loudness
            s.matchDb = std::clamp (opts.referenceLoudnessDb - loud, -LevelMatch::kMaxCorrectionDb, LevelMatch::kMaxCorrectionDb);
            s.matchGain = dbToGain (s.matchDb);

            if (cap->analysis && cap->analysis->valid)
            {
                if (refPolarity == 0) refPolarity = cap->analysis->polarity;
                s.analysisPolarity = cap->analysis->polarity == refPolarity ? 1 : -1;
                s.intrinsicLatency = (int) std::lround (cap->analysis->latencySamples * sr / cap->analysis->sampleRate);
            }
            sources.push_back (s);
        }

        // Level match: how loud each capture sounded on the guitar phrase.
        for (size_t i = 0; i < sources.size(); ++i)
            if (usedPrep[i]->loudnessDb > -120.0)
            {
                sources[i].matchDb = LevelMatch::correctionDb (usedPrep[i]->loudnessDb);
                sources[i].matchGain = dbToGain (sources[i].matchDb);
            }
        for (auto& s : sources) ch.info.matchDb.push_back (s.matchDb);

        // Phase matching: play one probe through every capture, time-align by latency, then measure
        // each capture's residual (sub-sample) lag and polarity against the first capture.
        if (sources.size() >= 2)
        {
            const int maxLag = (int) std::ceil (0.003 * sr);
            std::vector<std::vector<double>> resp (sources.size());
            for (size_t i = 0; i < sources.size(); ++i)
            {
                // remove this capture's own latency (exactly what Time-aligned mode does)
                const size_t tot = (size_t) std::max (0, sources[i].model->getResamplingLatency() + sources[i].intrinsicLatency);
                const auto& y = usedPrep[i]->probeOut;
                std::vector<double> a (probe.size(), 0.0);
                for (size_t k = 0; k < a.size(); ++k)
                    if (k + tot < y.size()) a[k] = y[k + tot];
                resp[i] = std::move (a);
            }
            for (size_t i = 1; i < sources.size(); ++i)
            {
                const auto e = PhaseMatcher::estimate (resp[0], resp[i], maxLag);
                if (e.correlation >= PhaseMatcher::kMinCorrelation)
                {
                    sources[i].phaseLag = e.lag;
                    sources[i].phasePolarity = e.polarity;
                }
                ch.phaseCorrelation.push_back (e.correlation);
            }
        }
        ch.blender.prepare (sr, maxBlock, std::move (sources));
        ch.blender.setAlwaysOn (opts.alwaysOnModels);
    }

    for (auto& ch : rt->channels) rt->latency = std::max (rt->latency, ch.blender.getLatency());
    for (auto& ch : rt->channels)
    {
        ch.pad.prepare (rt->latency - ch.blender.getLatency() + 1);
        ch.pad.setDelay (rt->latency - ch.blender.getLatency());
    }
    // start each channel at its saved knobs so the first block is already blended correctly
    for (size_t ci = 0; ci < def.channels.size(); ++ci)
    {
        rt->setKnobs ((int) ci, def.channels[ci].savedKnobs);
        if (rt->blend) { std::array<double, AmpDefinition::kMaxBlendModels> ones; ones.fill (1.0); rt->setBlendLevels ((int) ci, ones); }
        rt->channels[ci].blender.snapToTargets();
    }
    return rt;
}

ResidualAmounts AmpRuntime::setKnobs (int channel, const KnobCoords& k) noexcept
{
    ResidualAmounts r;
    if (channel < 0 || channel >= (int) channels.size()) return r;
    auto& ch = channels[(size_t) channel];
    if (blend)
    {
        // Blend weights come from the Mix knobs; every amp knob acts through the DSP stages.
        r.gain = k[(size_t) Control::Gain] - 5.0;
        r.bass = k[(size_t) Control::Bass] - 5.0;
        r.middle = k[(size_t) Control::Middle] - 5.0;
        r.treble = k[(size_t) Control::Treble] - 5.0;
        r.presence = k[(size_t) Control::Presence] - 5.0;
        r.resonance = k[(size_t) Control::Resonance] - 5.0;
        return r;
    }
    const auto res = ch.interp.compute (k);
    ch.blender.setTargets (res);

    auto delta = [&] (Control c) {
        const auto d = (size_t) c;
        if (res.covered[d]) return isKnown (res.effective[d]) ? k[d] - res.effective[d] : 0.0;
        return k[d] - ch.baseline[d];
    };
    r.gain = delta (Control::Gain);
    r.bass = delta (Control::Bass);
    r.middle = delta (Control::Middle);
    r.treble = delta (Control::Treble);
    r.presence = delta (Control::Presence);
    r.resonance = delta (Control::Resonance);
    return r;
}

void AmpRuntime::process (int channel, const double* in, double* out, int n) noexcept
{
    if (channel < 0 || channel >= (int) channels.size() || channels[(size_t) channel].blender.numSources() == 0)
    {
        std::copy (in, in + n, out); // empty channel: dry passthrough
        return;
    }
    auto& ch = channels[(size_t) channel];
    ch.blender.process (in, out, n);
    if (ch.pad.getDelay() > 0)
        for (int i = 0; i < n; ++i) out[i] = ch.pad.process (out[i]);
}

void AmpRuntime::setBlendLevels (int channel, const std::array<double, AmpDefinition::kMaxBlendModels>& levels) noexcept
{
    if (! blend || channel < 0 || channel >= (int) channels.size()) return;
    auto& ch = channels[(size_t) channel];
    InterpolationResult r;
    const int n = std::min ({ ch.blender.numSources(), (int) ch.info.slot.size(), AmpDefinition::kMaxBlendModels });
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += std::max (0.0, levels[(size_t) ch.info.slot[(size_t) i]]);
    for (int i = 0; i < n; ++i)
    {
        const double w = std::max (0.0, levels[(size_t) ch.info.slot[(size_t) i]]);
        if (w <= 0.0 || sum <= 0.0) continue;
        r.index[(size_t) r.numActive] = i;
        r.weight[(size_t) r.numActive] = w / sum; // normalised: the blend keeps a constant level
        ++r.numActive;
    }
    ch.blender.setTargets (r);
}

std::array<double, 5> AmpRuntime::toneCorrection (int channel, const std::array<double, AmpDefinition::kMaxBlendModels>& gain,
                                                  const std::array<std::array<double, AmpDefinition::kMaxBlendModels>, 5>& bandLevels) const noexcept
{
    if (! blend || channel < 0 || channel >= (int) channels.size()) return {};
    return toneCorrectionFor (channels[(size_t) channel].info, gain, bandLevels);
}

std::array<double, 5> AmpRuntime::toneCorrectionFor (const BlendChannelInfo& ch, const std::array<double, AmpDefinition::kMaxBlendModels>& gain,
                                                     const std::array<std::array<double, AmpDefinition::kMaxBlendModels>, 5>& bandLevels) noexcept
{
    std::array<double, 5> out {};
    const int n = std::min ({ (int) ch.bands.size(), (int) ch.slot.size(), AmpDefinition::kMaxBlendModels });
    if (n == 0) return out;
    auto g = [&] (int m) { return std::max (0.0, gain[(size_t) ch.slot[(size_t) m]]); };
    auto lv = [&] (int b, int m) { return std::max (0.0, bandLevels[(size_t) b][(size_t) ch.slot[(size_t) m]]); };

    double gs = 0.0;
    for (int m = 0; m < n; ++m) gs += g (m);
    for (int b = 0; b < 5; ++b)
    {
        // what the audible mix already has in this band
        double cur = 0.0;
        if (gs > 0.0)
            for (int m = 0; m < n; ++m) cur += g (m) / gs * ch.bands[(size_t) m][(size_t) b];
        // what the user wants this band to sound like
        double ws = 0.0, tgt = 0.0;
        for (int m = 0; m < n; ++m) ws += lv (b, m);
        if (ws <= 0.0) continue; // no source chosen for this band: leave it as it is
        for (int m = 0; m < n; ++m) tgt += lv (b, m) / ws * ch.bands[(size_t) m][(size_t) b];
        out[(size_t) b] = std::clamp (tgt - cur, -12.0, 12.0);
    }
    return out;
}

AmpRuntime::AutoBlend AmpRuntime::autoBlend (int channel, const KnobCoords& k) const noexcept
{
    if (! blend || channel < 0 || channel >= (int) channels.size()) return {};
    return autoBlendFor (channels[(size_t) channel].info, k);
}

std::vector<AmpRuntime::BlendChannelInfo> AmpRuntime::blendInfo() const
{
    std::vector<BlendChannelInfo> v;
    for (auto& ch : channels) v.push_back (ch.info);
    return v;
}

AmpRuntime::AutoBlend AmpRuntime::autoBlendFor (const BlendChannelInfo& ch, const KnobCoords& k) noexcept
{
    AutoBlend a;
    const int n = std::min ({ (int) ch.bands.size(), (int) ch.slot.size(), (int) ch.gainFeature.size(),
                              AmpDefinition::kMaxBlendModels, InterpolationResult::kMaxActive });
    if (n == 0) return a;

    // Sweep one knob (0..10) across the sources ordered by 'feature': piecewise-linear crossfade
    // between neighbours, so the knob is continuous and lands exactly on a capture at each step.
    auto sweep = [&] (double knob, auto feature, std::array<double, AmpDefinition::kMaxBlendModels>& out, double minSpread) {
        std::array<int, InterpolationResult::kMaxActive> order {};
        for (int i = 0; i < n; ++i) order[(size_t) i] = i;
        std::sort (order.begin(), order.begin() + n, [&] (int x, int y) { return feature (x) < feature (y); });
        out.fill (0.0);
        const double spread = feature (order[(size_t) n - 1]) - feature (order[0]);
        if (n == 1 || spread < minSpread)
        {
            for (int i = 0; i < n; ++i) out[(size_t) ch.slot[(size_t) i]] = 1.0; // nothing to choose between: even
            return false;
        }
        const double x = std::clamp (knob, 0.0, 10.0) / 10.0 * (n - 1);
        const int lo = std::min ((int) x, n - 2);
        const double f = x - lo;
        out[(size_t) ch.slot[(size_t) order[(size_t) lo]]] = 1.0 - f;
        out[(size_t) ch.slot[(size_t) order[(size_t) lo + 1]]] = f;
        return true;
    };

    a.varies[0] = sweep (k[(size_t) Control::Gain], [&] (int m) { return ch.gainFeature[(size_t) m]; }, a.gain, 3.0);
    const Control bandKnob[5] = { Control::Resonance, Control::Bass, Control::Middle, Control::Treble, Control::Presence };
    for (int b = 0; b < 5; ++b)
        a.varies[(size_t) b + 1] = sweep (k[(size_t) bandKnob[b]], [&] (int m) { return ch.bands[(size_t) m][(size_t) b]; },
                                          a.bands[(size_t) b], 1.0);
    return a;
}

void AmpRuntime::setPhaseMode (PhaseMode m) noexcept
{
    for (auto& ch : channels) ch.blender.setPhaseMode (m);
}

void AmpRuntime::setLevelMatch (bool on) noexcept
{
    for (auto& ch : channels) ch.blender.setLevelMatch (on);
}

double AmpRuntime::matchDb (int channel, int source) const noexcept
{
    if (channel < 0 || channel >= (int) channels.size()) return 0.0;
    const auto& ch = channels[(size_t) channel];
    return source >= 0 && source < ch.blender.numSources() ? ch.blender.source (source).matchDb : 0.0;
}

double AmpRuntime::matchDbForSlot (int channel, int slot) const noexcept
{
    if (channel < 0 || channel >= (int) channels.size()) return std::numeric_limits<double>::quiet_NaN();
    const auto& ch = channels[(size_t) channel];
    for (int i = 0; i < ch.blender.numSources() && i < (int) ch.info.slot.size(); ++i)
        if (ch.info.slot[(size_t) i] == slot) return ch.blender.source (i).matchDb;
    return std::numeric_limits<double>::quiet_NaN();
}

void AmpRuntime::setSlotPolarity (int channel, const std::array<bool, AmpDefinition::kMaxBlendModels>& invert) noexcept
{
    if (channel < 0 || channel >= (int) channels.size()) return;
    auto& ch = channels[(size_t) channel];
    for (int i = 0; i < ch.blender.numSources() && i < (int) ch.info.slot.size(); ++i)
        ch.blender.setUserPolarity (i, invert[(size_t) ch.info.slot[(size_t) i]] ? -1 : 1);
}

AmpRuntime::PhaseInfo AmpRuntime::phaseInfo (int channel, int source) const noexcept
{
    PhaseInfo p;
    if (channel < 0 || channel >= (int) channels.size()) return p;
    const auto& ch = channels[(size_t) channel];
    if (source <= 0 || source >= ch.blender.numSources()) return p;
    p.lag = ch.blender.source (source).phaseLag;
    p.polarity = ch.blender.source (source).phasePolarity;
    p.correlation = (size_t) source - 1 < ch.phaseCorrelation.size() ? ch.phaseCorrelation[(size_t) source - 1] : 0.0;
    return p;
}

bool AmpRuntime::isCovered (int channel, Control c) const noexcept
{
    return channel >= 0 && channel < (int) channels.size() && channels[(size_t) channel].interp.isCovered (c);
}

int AmpRuntime::numRunningModels (int channel) const noexcept
{
    return channel >= 0 && channel < (int) channels.size() ? channels[(size_t) channel].blender.numRunning() : 0;
}
} // namespace namknobs
