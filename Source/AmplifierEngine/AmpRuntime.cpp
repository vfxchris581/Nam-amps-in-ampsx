#include "AmpRuntime.h"

#include <algorithm>

namespace namvx
{
std::unique_ptr<AmpRuntime> AmpRuntime::build (const AmpDefinition& def, double sr, int maxBlock,
                                               const RuntimeOptions& opts, std::vector<std::string>& errors)
{
    auto rt = std::make_unique<AmpRuntime>();
    rt->ampId = def.id;
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
                coords.push_back (cap.coords);
                enabled.push_back (true);
                used.push_back (&cap);
            }
            catch (const std::exception& e) { errors.push_back (e.what()); }
        }

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
        rt->channels[ci].blender.snapToTargets();
    }
    return rt;
}

ResidualAmounts AmpRuntime::setKnobs (int channel, const KnobCoords& k) noexcept
{
    ResidualAmounts r;
    if (channel < 0 || channel >= (int) channels.size()) return r;
    auto& ch = channels[(size_t) channel];
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

bool AmpRuntime::isCovered (int channel, Control c) const noexcept
{
    return channel >= 0 && channel < (int) channels.size() && channels[(size_t) channel].interp.isCovered (c);
}

int AmpRuntime::numRunningModels (int channel) const noexcept
{
    return channel >= 0 && channel < (int) channels.size() ? channels[(size_t) channel].blender.numRunning() : 0;
}
} // namespace namvx
