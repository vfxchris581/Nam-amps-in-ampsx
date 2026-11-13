#include "DualCabinet.h"

namespace namknobs
{
void DualCabinet::prepare (const juce::dsp::ProcessSpec& spec)
{
    slot1.prepare (spec);
    slot2.prepare (spec);
    b1.setSize (2, (int) spec.maximumBlockSize);
    b2.setSize (2, (int) spec.maximumBlockSize);
    for (auto* s : { &w1, &w2, &level }) s->reset (spec.sampleRate, 0.03);
}

void DualCabinet::reset() { slot1.reset(); slot2.reset(); }

void DualCabinet::setSlotParams (int i, IRSlot::Params p) noexcept
{
    (i == 0 ? p1 : p2) = p;
}

void DualCabinet::process (const float* monoIn, juce::AudioBuffer<float>& out, int n) noexcept
{
    if (! global.enabled || (! slot1.isLoaded() && ! slot2.isLoaded()))
    {
        out.copyFrom (0, 0, monoIn, n);
        out.copyFrom (1, 0, monoIn, n);
        return;
    }

    auto q1 = p1, q2 = p2;
    q1.alignMs = global.autoAlign ? align1.load() : 0.0f;
    q2.alignMs = global.autoAlign ? align2.load() : 0.0f;
    slot1.setParams (q1);
    slot2.setParams (q2);
    slot1.process (monoIn, b1, n);
    slot2.process (monoIn, b2, n);

    const bool has1 = slot1.isLoaded() && p1.enabled, has2 = slot2.isLoaded() && p2.enabled;
    const float b = juce::jlimit (0.0f, 1.0f, global.blend);
    float t1 = juce::jmin (1.0f, 2.0f * (1.0f - b)), t2 = juce::jmin (1.0f, 2.0f * b);
    if (has1 && ! has2) { t1 = 1.0f; t2 = 0.0f; }
    if (has2 && ! has1) { t1 = 0.0f; t2 = 1.0f; }
    // normalised crossfade: two coherent cabinets never sum louder than one
    const float sum = juce::jmax (1.0e-6f, t1 + t2);
    w1.setTargetValue (t1 / sum);
    w2.setTargetValue (t2 / sum);
    level.setTargetValue (juce::Decibels::decibelsToGain (global.levelDb, -100.0f));

    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float a = w1.getNextValue(), c = w2.getNextValue(), g = level.getNextValue();
        l[i] = g * (a * b1.getSample (0, i) + c * b2.getSample (0, i));
        r[i] = g * (a * b1.getSample (1, i) + c * b2.getSample (1, i));
        if (! global.stereo) { const float m = 0.5f * (l[i] + r[i]); l[i] = r[i] = m; }
    }
}

DualCabinet::Alignment DualCabinet::computeAlignment() const
{
    Alignment al;
    if (! slot1.isLoaded() || ! slot2.isLoaded()) return al;

    // bring both IRs to a common analysis rate (96 kHz) for sub-sample-ish resolution
    const double rate = 96000.0;
    auto resampled = [rate] (const juce::AudioBuffer<float>& src, double srcRate) {
        const double ratio = srcRate / rate;
        const int len = std::min ((int) (src.getNumSamples() / ratio), (int) (0.05 * rate)); // first 50 ms
        std::vector<float> out ((size_t) std::max (0, len), 0.0f);
        juce::LagrangeInterpolator interp;
        if (len > 0) interp.process (ratio, src.getReadPointer (0), out.data(), len);
        return out;
    };
    const auto a = resampled (slot1.getAnalysisCopy(), slot1.getInfo().sampleRate);
    const auto b = resampled (slot2.getAnalysisCopy(), slot2.getInfo().sampleRate);
    if (a.empty() || b.empty()) return al;

    const int maxLag = (int) (0.003 * rate);
    double best = 0.0; int bestLag = 0;
    double ea = 0, eb = 0;
    for (float v : a) ea += (double) v * v;
    for (float v : b) eb += (double) v * v;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i < (int) a.size(); ++i)
        {
            const int j = i + lag;
            if (j >= 0 && j < (int) b.size()) s += (double) a[(size_t) i] * b[(size_t) j];
        }
        if (std::abs (s) > std::abs (best)) { best = s; bestLag = lag; }
    }
    al.correlation = (float) (best / std::sqrt (ea * eb + 1.0e-30));
    // Two unrelated cabinets don't share a meaningful arrival time: leave them as they are.
    if (std::abs (al.correlation) < 0.3f) return Alignment { 0, 0, false, al.correlation };
    al.polarityMismatch = best < 0.0;
    const float ms = (float) (1000.0 * bestLag / rate);
    // positive lag: IR2 arrives later → delay IR1
    if (ms > 0) al.slot1DelayMs = ms; else al.slot2DelayMs = -ms;
    return al;
}

void DualCabinet::updateAlignment()
{
    alignment = computeAlignment();
    align1 = alignment.slot1DelayMs;
    align2 = alignment.slot2DelayMs;
}
} // namespace namknobs
