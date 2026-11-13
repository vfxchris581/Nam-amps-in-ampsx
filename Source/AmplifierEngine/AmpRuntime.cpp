#include "AmpRuntime.h"

#include <algorithm>
#include "ProfileAnalyzer/ProfileAnalyzer.h"

namespace namvx
{
std::unique_ptr<AmpRuntime> AmpRuntime::build (const AmpDefinition& def, double sr, int maxBlock,
                                               const RuntimeOptions& opts, std::vector<std::string>& errors)
{
    auto rt = std::make_unique<AmpRuntime>();
    rt->ampId = def.id;
    rt->blend = def.isBlend;
    rt->channels.resize (def.channels.size());

    for (size_t ci = 0; ci < def.channels.size(); ++ci)
    {
        const auto& chDef = def.channels[ci];
        auto& ch = rt->channels[ci];
        std::vector<KnobCoords> coords;
        std::vector<bool> enabled;
        std::vector<const CaptureEntry*> used;

        for (const auto& cap : chDef.captures)
        {
            if (! cap.enabled) continue;
            try
            {
                auto m = std::make_unique<NamModel>();
                m->load (cap.path);
                m->prepare (sr, maxBlock);
                ch.models.push_back (std::move (m));
                std::array<double, 5> bd {};
                if (cap.analysis && cap.analysis->valid)
                    for (int b = 0; b < 5; ++b) bd[(size_t) b] = cap.analysis->bandsDb[(size_t) b];
                ch.bands.push_back (bd);
                ch.gainFeature.push_back (cap.analysis && cap.analysis->valid ? ProfileAnalyzer::featureFor (Control::Gain, *cap.analysis) : 0.0);
                ch.slot.push_back (std::clamp (cap.blendSlot >= 0 ? cap.blendSlot : (int) ch.slot.size(), 0, AmpDefinition::kMaxBlendModels - 1));
                coords.push_back (cap.coords);
                enabled.push_back (true);
                used.push_back (&cap);
            }
            catch (const std::exception& e) { errors.push_back (e.what()); }
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
            s.staticGain = dbToGain (opts.loudnessCompensation * (opts.referenceLoudnessDb - loud) + cap->trimDb);

            if (cap->analysis && cap->analysis->valid)
            {
                if (refPolarity == 0) refPolarity = cap->analysis->polarity;
                if (cap->analysis->polarity != refPolarity) s.staticGain = -s.staticGain;
                s.intrinsicLatency = (int) std::lround (cap->analysis->latencySamples * sr / cap->analysis->sampleRate);
            }
            sources.push_back (s);
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
    const int n = std::min ({ ch.blender.numSources(), (int) ch.slot.size(), AmpDefinition::kMaxBlendModels });
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += std::max (0.0, levels[(size_t) ch.slot[(size_t) i]]);
    for (int i = 0; i < n; ++i)
    {
        const double w = std::max (0.0, levels[(size_t) ch.slot[(size_t) i]]);
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
    std::array<double, 5> out {};
    if (! blend || channel < 0 || channel >= (int) channels.size()) return out;
    const auto& ch = channels[(size_t) channel];
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
    AutoBlend a;
    if (! blend || channel < 0 || channel >= (int) channels.size()) return a;
    const auto& ch = channels[(size_t) channel];
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

bool AmpRuntime::isCovered (int channel, Control c) const noexcept
{
    return channel >= 0 && channel < (int) channels.size() && channels[(size_t) channel].interp.isCovered (c);
}

int AmpRuntime::numRunningModels (int channel) const noexcept
{
    return channel >= 0 && channel < (int) channels.size() ? channels[(size_t) channel].blender.numRunning() : 0;
}
} // namespace namvx
