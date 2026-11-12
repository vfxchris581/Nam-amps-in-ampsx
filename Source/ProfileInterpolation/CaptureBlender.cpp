#include "CaptureBlender.h"

#include <algorithm>
#include <cmath>

namespace namvx
{
void CaptureBlender::prepare (double sampleRate, int maxBlockSize, std::vector<BlendSource> sources,
                              double fadeMs, double holdMs)
{
    src = std::move (sources);
    const size_t n = src.size();
    w.assign (n, 0.0);
    target.assign (n, 0.0);
    hold.assign (n, 0);
    tmp.assign ((size_t) maxBlockSize, 0.0);
    // One-pole glide shared by every weight: a linear map, so the weights keep summing to one
    // during every transition (no level dips while crossfading).
    rampPerSample = 1.0 - std::exp (-3.0 / std::max (1.0, fadeMs * 0.001 * sampleRate));
    holdSamples = (int) (holdMs * 0.001 * sampleRate);

    int maxTotal = 0, minIntrinsic = 1 << 30;
    for (auto& s : src)
    {
        const int total = (s.model ? s.model->getResamplingLatency() : 0) + s.intrinsicLatency;
        maxTotal = std::max (maxTotal, total);
        minIntrinsic = std::min (minIntrinsic, s.intrinsicLatency);
    }
    align.assign (n, IntDelay());
    for (size_t i = 0; i < n; ++i)
    {
        const int total = (src[i].model ? src[i].model->getResamplingLatency() : 0) + src[i].intrinsicLatency;
        align[i].prepare (maxTotal - total + 1);
        align[i].setDelay (maxTotal - total);
    }
    reportedLatency = n > 0 ? maxTotal - minIntrinsic : 0;

    if (n > 0) { w[0] = target[0] = 1.0; }
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

void CaptureBlender::process (const double* in, double* out, int n) noexcept
{
    std::fill (out, out + n, 0.0);
    running = 0;
    for (size_t i = 0; i < src.size(); ++i)
    {
        if (target[i] > 0.0 || w[i] > 0.0) hold[i] = holdSamples;
        else if (hold[i] > 0) hold[i] = std::max (0, hold[i] - n);

        const bool active = alwaysOn || target[i] > 0.0 || w[i] > 0.0 || hold[i] > 0;
        if (! active || src[i].model == nullptr) continue;
        ++running;

        src[i].model->process (in, tmp.data(), n);

        const double g = src[i].staticGain;
        double wi = w[i];
        const double t = target[i];
        for (int s = 0; s < n; ++s)
        {
            wi += (t - wi) * rampPerSample;
            if (std::abs (t - wi) < 1.0e-6) wi = t;
            const double y = align[i].process (tmp[(size_t) s]);
            out[s] += wi * g * y;
        }
        w[i] = wi;
    }
}
} // namespace namvx
