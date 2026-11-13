#pragma once
// Phase matching between NAM captures that are blended together.
//
// Blending two captures sums their outputs; if they are offset in time (different reamp latency,
// different amp/mic phase response) or inverted, frequencies cancel ("bad phase", comb filtering).
// The matcher plays the same low-level, band-limited probe through every capture, then finds the
// lag (to a fraction of a sample, by parabolic peak interpolation of the cross-correlation) and the
// polarity that best lines each capture up with the reference capture.

#include <vector>

namespace namvx
{
struct LagEstimate
{
    double lag = 0.0;      // samples; positive = 'sig' arrives later than 'ref'
    int polarity = 1;      // -1 if 'sig' is inverted relative to 'ref'
    double correlation = 0.0; // normalised |peak| (0..1)
};

class PhaseMatcher
{
public:
    /** Cross-correlation lag estimate within +-maxLag samples. */
    static LagEstimate estimate (const std::vector<double>& ref, const std::vector<double>& sig, int maxLag);

    /** Deterministic probe: band-limited (80 Hz - 3 kHz) noise at about -30 dBFS RMS. */
    static std::vector<double> makeProbe (double sampleRate, double seconds = 0.35);

    /** Minimum correlation for a correction to be trusted (unrelated captures are left alone). */
    static constexpr double kMinCorrelation = 0.2;
};
} // namespace namvx
