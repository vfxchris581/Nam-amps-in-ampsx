#include "PhaseMatcher.h"

#include <algorithm>
#include <cmath>
#include <random>
#include "AudioEngine/DspCommon.h"

namespace namknobs
{
LagEstimate PhaseMatcher::estimate (const std::vector<double>& ref, const std::vector<double>& sig, int maxLag)
{
    LagEstimate e;
    const int n = (int) std::min (ref.size(), sig.size());
    if (n <= 2 * maxLag + 8) return e;

    double er = 0.0, es = 0.0;
    for (int i = maxLag; i < n - maxLag; ++i) { er += ref[(size_t) i] * ref[(size_t) i]; es += sig[(size_t) i] * sig[(size_t) i]; }
    if (er <= 1e-20 || es <= 1e-20) return e;

    std::vector<double> c ((size_t) (2 * maxLag + 1), 0.0);
    int best = maxLag;
    for (int k = -maxLag; k <= maxLag; ++k)
    {
        double s = 0.0;
        for (int i = maxLag; i < n - maxLag; ++i) s += ref[(size_t) i] * sig[(size_t) (i + k)];
        c[(size_t) (k + maxLag)] = s;
        if (std::abs (s) > std::abs (c[(size_t) best])) best = k + maxLag;
    }
    const double peak = c[(size_t) best];
    e.polarity = peak >= 0.0 ? 1 : -1;
    e.correlation = std::abs (peak) / std::sqrt (er * es);

    // parabolic interpolation on the (polarity-corrected) peak for sub-sample accuracy
    double frac = 0.0;
    if (best > 0 && best < (int) c.size() - 1)
    {
        const double y0 = e.polarity * c[(size_t) best - 1], y1 = e.polarity * c[(size_t) best], y2 = e.polarity * c[(size_t) best + 1];
        const double den = y0 - 2.0 * y1 + y2;
        if (std::abs (den) > 1e-30) frac = std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
    }
    e.lag = (best - maxLag) + frac;
    return e;
}

std::vector<double> PhaseMatcher::makeProbe (double sr, double seconds)
{
    const size_t n = (size_t) (sr * seconds);
    std::vector<double> x (n);
    std::mt19937 rng (20261112);
    std::normal_distribution<double> nd (0.0, 1.0);
    for (auto& v : x) v = nd (rng);
    // band-limit: 80 Hz high-pass, 3 kHz low-pass (2nd order each, applied twice)
    Biquad hp1, hp2, lp1, lp2;
    hp1.setCoeffs (Biquad::highPass (sr, 80.0, 0.7071)); hp2.setCoeffs (Biquad::highPass (sr, 80.0, 0.7071));
    lp1.setCoeffs (Biquad::lowPass (sr, 3000.0, 0.7071)); lp2.setCoeffs (Biquad::lowPass (sr, 3000.0, 0.7071));
    double ss = 0.0;
    for (auto& v : x) { v = lp2.process (lp1.process (hp2.process (hp1.process (v)))); ss += v * v; }
    const double g = dbToGain (-30.0) / std::sqrt (ss / (double) n + 1e-30);
    // short fades so the burst starts and ends cleanly
    const size_t fade = (size_t) (0.01 * sr);
    for (size_t i = 0; i < n; ++i)
    {
        double w = 1.0;
        if (i < fade) w = (double) i / fade;
        if (i + fade > n) w = (double) (n - i) / fade;
        x[i] *= g * w;
    }
    return x;
}
} // namespace namknobs
