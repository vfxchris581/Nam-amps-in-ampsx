#include "ProfileInterpolator.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace namvx
{
void ProfileInterpolator::setCaptures (const std::vector<KnobCoords>& coords, const std::vector<bool>& enabled)
{
    points.clear();
    for (size_t i = 0; i < coords.size(); ++i)
        if (i >= enabled.size() || enabled[i]) points.push_back ({ coords[i], true, (int) i });

    for (int d = 0; d < kNumControls; ++d)
    {
        std::vector<double> vals;
        for (auto& p : points)
            if (isKnown (p.c[(size_t) d])) vals.push_back (p.c[(size_t) d]);
        std::sort (vals.begin(), vals.end());
        std::set<long> distinct;
        for (double v : vals) distinct.insert (std::lround (v * 100.0));
        covered[(size_t) d] = distinct.size() >= 2;
        lo[(size_t) d] = vals.empty() ? 0.0 : vals.front();
        hi[(size_t) d] = vals.empty() ? 10.0 : vals.back();
        range[(size_t) d] = vals.empty() ? 1.0 : std::max (1.0, vals.back() - vals.front());
        fallback[(size_t) d] = vals.empty() ? 5.0 : vals[vals.size() / 2];
    }
    scratch.reserve (points.size() + 1);
}

InterpolationResult ProfileInterpolator::compute (const KnobCoords& knobs) const
{
    InterpolationResult r;
    r.covered = covered;
    r.effective = unknownCoords();
    if (points.empty()) return r;

    if (points.size() == 1)
    {
        r.numActive = 1; r.index[0] = points[0].originalIndex; r.weight[0] = 1.0;
        for (int d = 0; d < kNumControls; ++d) r.effective[(size_t) d] = points[0].c[(size_t) d];
        return r;
    }

    // Normalised distances over covered dimensions. Knob positions beyond the captured range
    // are clamped for selection (the overshoot is realised by the residual DSP instead).
    auto& dist = scratch;
    dist.clear();
    for (size_t i = 0; i < points.size(); ++i)
    {
        double s = 0.0;
        for (int d = 0; d < kNumControls; ++d)
        {
            if (! covered[(size_t) d]) continue;
            const double pc = isKnown (points[i].c[(size_t) d]) ? points[i].c[(size_t) d] : fallback[(size_t) d];
            const double k = std::clamp (knobs[(size_t) d], lo[(size_t) d], hi[(size_t) d]);
            const double diff = (k - pc) / range[(size_t) d];
            s += diff * diff;
        }
        dist.push_back ({ std::sqrt (s), (int) i });
    }
    std::sort (dist.begin(), dist.end(), [] (const DI& a, const DI& b) { return a.d < b.d; });

    const int K = std::min (maxActive, (int) dist.size());
    std::array<double, InterpolationResult::kMaxActive> w {};
    double sum = 0.0;

    if (dist[0].d < 1.0e-6)
    {
        w[0] = 1.0; sum = 1.0; r.numActive = 1;
        r.index[0] = dist[0].i;
    }
    else
    {
        const double cut = (size_t) K < dist.size() ? std::pow (dist[(size_t) K].d, -power) : 0.0;
        for (int k = 0; k < K; ++k)
        {
            const double v = std::max (0.0, std::pow (dist[(size_t) k].d, -power) - cut);
            if (v <= 0.0) continue;
            r.index[(size_t) r.numActive] = dist[(size_t) k].i;
            w[(size_t) r.numActive] = v;
            sum += v;
            ++r.numActive;
        }
        if (r.numActive == 0) { r.numActive = 1; r.index[0] = dist[0].i; w[0] = sum = 1.0; }
    }

    for (int k = 0; k < r.numActive; ++k)
    {
        r.weight[(size_t) k] = w[(size_t) k] / sum;
        r.index[(size_t) k] = points[(size_t) r.index[(size_t) k]].originalIndex;
    }

    for (int d = 0; d < kNumControls; ++d)
    {
        double acc = 0.0, wsum = 0.0;
        for (int k = 0; k < r.numActive; ++k)
        {
            const Point* p = nullptr;
            for (auto& pp : points) if (pp.originalIndex == r.index[(size_t) k]) { p = &pp; break; }
            if (p && isKnown (p->c[(size_t) d])) { acc += r.weight[(size_t) k] * p->c[(size_t) d]; wsum += r.weight[(size_t) k]; }
        }
        r.effective[(size_t) d] = wsum > 0.0 ? acc / wsum : std::nan ("");
    }
    return r;
}
} // namespace namvx
