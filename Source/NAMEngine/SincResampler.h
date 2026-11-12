#pragma once
// Streaming arbitrary-ratio windowed-sinc (Kaiser) resampler.
// Used to run NAM models at the sample rate they were trained at, whatever the host rate.

#include <vector>
#include "AudioEngine/DspCommon.h"

namespace namvx
{
class SincResampler
{
public:
    /** halfTaps: zero crossings per side at the passband edge. 32 gives > 100 dB stopband. */
    void prepare (double inRate, double outRate, int maxInputBlock, int halfTaps = 32);
    void reset();

    /** Consumes n input samples, pushes every output sample that can be produced into 'out'. */
    void process (const double* in, int n, SampleFifo& out) noexcept;

    /** Group delay in *input* samples. */
    double latencyInInputSamples() const noexcept { return halfWidth; }
    double getRatio() const noexcept { return step; }

private:
    double kernel (double x) const noexcept;

    std::vector<double> table; // kernel sampled at kTableRes points per input sample
    static constexpr int kTableRes = 512;
    std::vector<double> hist;
    size_t histCount = 0;
    double step = 1.0;      // input samples advanced per output sample
    double cutoff = 1.0;    // fraction of input Nyquist
    double halfWidth = 32;  // kernel half width in input samples
    double pos = 0.0;       // read position (input samples) relative to hist[0]
};
} // namespace namvx
