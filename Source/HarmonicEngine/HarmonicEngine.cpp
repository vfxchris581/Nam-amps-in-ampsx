#include "HarmonicEngine.h"

namespace namknobs
{
// Harmonics are generated with envelope-normalised Chebyshev polynomials: for a sinusoid of
// amplitude A, A·T_k(x/A) is a pure k-th harmonic, so the engine *adds* harmonic detail
// without stealing level from the fundamental (the capture's own tone stays intact). On real,
// polyphonic material the same polynomials produce the intermodulation a tube stage would.
// Harmonic depth rises with playing level (touch) and is ducked on pick transients.

namespace
{
inline double t2 (double u) noexcept { return 2.0 * u * u - 1.0; }
inline double t3 (double u) noexcept { return u * (4.0 * u * u - 3.0); }
inline double t4 (double u) noexcept { const double u2 = u * u; return 8.0 * u2 * u2 - 8.0 * u2 + 1.0; }
inline double t5 (double u) noexcept { const double u2 = u * u; return u * (16.0 * u2 * u2 - 20.0 * u2 + 5.0); }
// keep |u| <= ~1.1 smoothly (envelope lags fast attacks)
inline double softLimit (double u) noexcept { return u / std::sqrt (1.0 + 0.5 * u * u * u * u) ; }
} // namespace

void HarmonicEngine::prepare (double sampleRate)
{
    fs = sampleRate;
    // Bands sum back exactly (mid = x - low - high), harmonics are added on top.
    lowLP1.setCoeffs (Biquad::lowPass (fs, 180.0, 0.7071));
    lowLP2.setCoeffs (Biquad::lowPass (fs, 180.0, 0.7071));
    highHP1.setCoeffs (Biquad::highPass (fs, 3200.0, 0.7071));
    highHP2.setCoeffs (Biquad::highPass (fs, 3200.0, 0.7071));
    tightHP.setCoeffs (Biquad::highPass (fs, 120.0, 0.7071));
    envFast.prepare (fs, 0.3, 12.0);
    envSlow.prepare (fs, 18.0, 120.0);
    envLevel.prepare (fs, 0.5, 40.0);
    envLow.prepare (fs, 2.0, 80.0);
    envHigh.prepare (fs, 0.3, 20.0);
    dc.prepare (fs, 6.0);
    reset();
}

void HarmonicEngine::reset()
{
    lowLP1.reset(); lowLP2.reset(); highHP1.reset(); highHP2.reset(); tightHP.reset();
    envFast.reset(); envSlow.reset(); envLevel.reset(); envLow.reset(); envHigh.reset(); dc.reset();
}

double HarmonicEngine::shape (double x, double drive, double bias) noexcept
{
    const double tb = std::tanh (drive * bias);
    const double slope = drive * (1.0 - tb * tb);
    return (std::tanh (drive * (x + bias)) - tb) / std::max (1.0e-9, slope);
}

double HarmonicEngine::process (double x) noexcept
{
    if (set.amount <= 0.0) return x;

    const double low = lowLP2.process (lowLP1.process (x));
    const double high = highHP2.process (highHP1.process (x));
    const double mid = x - low - high;

    // Envelopes: peak level of each band (Chebyshev normalisation) + transient detector.
    const double A = std::max (1.0e-5, envLevel.process (mid));
    const double AL = std::max (1.0e-5, envLow.process (low));
    const double AH = std::max (1.0e-5, envHigh.process (high));
    const double fast = envFast.process (x), slow = envSlow.process (x);
    const double transient = std::clamp ((fast - 1.3 * slow) / (fast + 1.0e-6), 0.0, 1.0);

    // Touch: harder playing → more harmonic complexity (tube-like breakup).
    const double touch = 0.25 + set.touch * std::clamp (2.5 * A, 0.0, 1.5);
    const double depth = set.amount * touch;

    const double u = softLimit (mid / A);
    const double even = 0.55 * t2 (u) + 0.18 * t4 (u);
    const double odd = 0.55 * t3 (u) + 0.15 * t5 (u);
    double harm = A * depth * (set.evenOdd * even + (1.0 - set.evenOdd) * odd);

    // High band: gentle odd "air" for pick and string detail.
    harm += AH * set.highDetail * 0.6 * set.amount * t3 (softLimit (high / AH));
    // Low band: Low Definition keeps it clean; otherwise a little 3rd for weight.
    harm += AL * (1.0 - set.lowDefinition) * 0.3 * set.amount * t3 (softLimit (low / AL));

    harm = tightHP.process (harm); // never muddy the sub-lows
    const double y = x + (1.0 - set.attackPreserve * transient) * harm;
    return dc.process (y);
}
} // namespace namknobs
