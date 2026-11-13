#include "CaptureBlender.h"

#include <algorithm>
#include <cmath>

namespace namknobs
{
// ---- FracDelay --------------------------------------------------------------------------------
void FracDelay::prepare (int maxDelaySamples)
{
    size = std::max (8, maxDelaySamples + 8);
    buf.assign ((size_t) size, 0.0);
    w = 0;
}

void FracDelay::reset() { std::fill (buf.begin(), buf.end(), 0.0); }

double FracDelay::process (double x) noexcept
{
    // ~40 ms glide toward the target delay (only moves when the phase mode changes)
    if (current != target)
    {
        const double d = target - current;
        current = std::abs (d) < 1e-4 ? target : current + d * 0.0015;
    }
    buf[(size_t) w] = x;
    const int di = (int) current;
    const double f = current - di;
    auto at = [&] (int k) { return buf[(size_t) ((w - k + size * 4) % size)]; };
    // 4-point Lagrange around the read position (taps at di-1 .. di+2), f = fractional part
    const double xm1 = at (di - 1), x0 = at (di), x1 = at (di + 1), x2 = at (di + 2);
    const double y = x0 + f * (-xm1 / 3.0 - x0 / 2.0 + x1 - x2 / 6.0)
                     + f * f * (xm1 / 2.0 - x0 + x1 / 2.0)
                     + f * f * f * (-xm1 / 6.0 + x0 / 2.0 - x1 / 2.0 + x2 / 6.0);
    w = (w + 1) % size;
    return y;
}

// ---- CaptureBlender ---------------------------------------------------------------------------
void CaptureBlender::prepare (double sampleRate, int maxBlockSize, std::vector<BlendSource> sources,
                              double fadeMs, double holdMs)
{
    src = std::move (sources);
    const size_t n = src.size();
    w.assign (n, 0.0);
    target.assign (n, 0.0);
    sign.assign (n, 1.0);
    signTarget.assign (n, 1.0);
    userSign.assign (n, 1);
    hold.assign (n, 0);
    totalLatency.assign (n, 0);
    tmp.assign ((size_t) maxBlockSize, 0.0);
    matchStep = 1.0 / std::max (1.0, 0.05 * sampleRate);
    // One-pole glide shared by every weight: a linear map, so the weights keep summing to one
    // during every transition (no level dips while crossfading).
    rampPerSample = 1.0 - std::exp (-3.0 / std::max (1.0, fadeMs * 0.001 * sampleRate));
    signRamp = 1.0 - std::exp (-3.0 / std::max (1.0, 0.03 * sampleRate));
    holdSamples = (int) (holdMs * 0.001 * sampleRate);

    // Headroom for phase-matching corrections (both directions), 3 ms.
    headroom = (int) std::ceil (0.003 * sampleRate) + 2;
    maxTotal = 0;
    int minIntrinsic = 1 << 30;
    for (size_t i = 0; i < n; ++i)
    {
        totalLatency[i] = (src[i].model ? src[i].model->getResamplingLatency() : 0) + src[i].intrinsicLatency;
        maxTotal = std::max (maxTotal, totalLatency[i]);
        minIntrinsic = std::min (minIntrinsic, src[i].intrinsicLatency);
    }
    align.assign (n, FracDelay());
    for (size_t i = 0; i < n; ++i)
    {
        align[i].prepare (maxTotal - totalLatency[i] + 2 * headroom + 4);
        align[i].setTarget (delayFor (i, mode));
        align[i].snap();
        sign[i] = signTarget[i] = signFor (i);
    }
    reportedLatency = n > 0 ? maxTotal + headroom - minIntrinsic : 0;
    if (n > 0) { w[0] = target[0] = 1.0; }
}

double CaptureBlender::delayFor (size_t i, PhaseMode m) const noexcept
{
    const double base = (double) headroom;
    switch (m)
    {
        case PhaseMode::Off:          return base + (double) (maxTotal - totalLatency[0]); // same delay for all
        case PhaseMode::TimeAligned:  return base + (double) (maxTotal - totalLatency[i]);
        case PhaseMode::PhaseMatched:
        default:
        {
            // source arrives phaseLag samples after the reference -> give it that much less delay
            const double c = std::clamp (-src[i].phaseLag, -(double) headroom + 1.0, (double) headroom - 1.0);
            return base + (double) (maxTotal - totalLatency[i]) + c;
        }
    }
}

double CaptureBlender::signFor (size_t i) const noexcept
{
    const int measured = mode == PhaseMode::PhaseMatched ? src[i].phasePolarity
                       : mode == PhaseMode::TimeAligned ? src[i].analysisPolarity : 1;
    return (double) (measured * userSign[i]);
}

void CaptureBlender::reset()
{
    for (auto& d : align) d.reset();
}

void CaptureBlender::setTargets (const InterpolationResult& r) noexcept
{
    std::fill (target.begin(), target.end(), 0.0);
    for (int k = 0; k < r.numActive; ++k)
    {
        const int idx = r.index[(size_t) k];
        if (idx >= 0 && idx < (int) target.size()) target[(size_t) idx] = r.weight[(size_t) k];
    }
}

void CaptureBlender::setUserPolarity (int i, int s) noexcept
{
    if (i < 0 || i >= (int) userSign.size()) return;
    userSign[(size_t) i] = s < 0 ? -1 : 1;
    signTarget[(size_t) i] = signFor ((size_t) i);
}

void CaptureBlender::setPhaseMode (PhaseMode m) noexcept
{
    if (m == mode) return;
    mode = m;
    for (size_t i = 0; i < src.size(); ++i)
    {
        align[i].setTarget (delayFor (i, mode));
        signTarget[i] = signFor (i);
    }
}

void CaptureBlender::snapToTargets() noexcept
{
    w = target;
    sign = signTarget;
    for (auto& d : align) d.snap();
}

void CaptureBlender::process (const double* in, double* out, int n) noexcept
{
    std::fill (out, out + n, 0.0);
    running = 0;
    // level match amount glides 0 <-> 1 over 50 ms
    const double a0 = matchAmt;
    const double a1 = a0 + std::clamp (matchTarget - a0, -matchStep * n, matchStep * n);
    matchAmt = a1;
    const double aStep = n > 0 ? (a1 - a0) / n : 0.0;
    for (size_t i = 0; i < src.size(); ++i)
    {
        if (target[i] > 0.0 || w[i] > 0.0) hold[i] = holdSamples;
        else if (hold[i] > 0) hold[i] = std::max (0, hold[i] - n);

        const bool active = alwaysOn || target[i] > 0.0 || w[i] > 0.0 || hold[i] > 0;
        if (! active || src[i].model == nullptr) continue;
        ++running;

        src[i].model->process (in, tmp.data(), n);

        const double g = src[i].staticGain, mg = src[i].matchGain - 1.0;
        double wi = w[i], si = sign[i], a = a0;
        const double t = target[i], st = signTarget[i];
        for (int s = 0; s < n; ++s)
        {
            wi += (t - wi) * rampPerSample;
            if (std::abs (t - wi) < 1.0e-6) wi = t;
            si += (st - si) * signRamp; // polarity flips pass smoothly through zero
            if (std::abs (st - si) < 1.0e-6) si = st;
            a += aStep;
            const double y = align[i].process (tmp[(size_t) s]);
            out[s] += wi * si * g * (1.0 + a * mg) * y;
        }
        w[i] = wi;
        sign[i] = si;
    }
}
} // namespace namknobs
