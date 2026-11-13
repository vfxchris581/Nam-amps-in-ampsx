#pragma once
// Seven-band parametric studio EQ (64-bit):
//   Low cut (12/24/48 dB/oct Butterworth) · Low shelf · three Bells · High shelf · High cut
// Frequencies glide on a log scale, gains/Q glide linearly, coefficients update in 32-sample
// steps, so automation and dragging are click-free. Works mono (pre-cab) or stereo (post-cab).

#include <array>
#include "DspCommon.h"

namespace namvx
{
struct EqBand
{
    bool on = true;
    double freq = 1000.0, gainDb = 0.0, q = 0.9;
    int slope = 1; // cuts: 0 = 12, 1 = 24, 2 = 48 dB/oct
};

struct EqSettings
{
    static constexpr int kBands = 7;
    enum Band { LowCut = 0, LowShelf, Bell1, Bell2, Bell3, HighShelf, HighCut };
    bool enabled = true;
    bool postCab = true;
    double outputDb = 0.0;
    std::array<EqBand, kBands> band { {
        { false, 80.0, 0.0, 0.707, 1 },  { true, 110.0, 0.0, 0.7, 1 },  { true, 400.0, 0.0, 0.9, 1 },
        { true, 1500.0, 0.0, 0.9, 1 },   { true, 4000.0, 0.0, 0.9, 1 }, { true, 7500.0, 0.0, 0.7, 1 },
        { false, 11000.0, 0.0, 0.707, 1 } } };
};

class ParametricEQ
{
public:
    void prepare (double sampleRate);
    void reset();
    void setSettings (const EqSettings& s) noexcept { target = s; }

    /** Mono (pre-cab path). */
    void process (double* x, int n) noexcept;
    /** Stereo (post-cab path). */
    void process (float* l, float* r, int n) noexcept;

    /** Magnitude response (dB) of a settings set at frequency f: used by the UI curve and tests. */
    static double responseDb (const EqSettings& s, double sampleRate, double f);
    /** Biquad sections of one band (up to 4 for a 48 dB/oct cut). */
    static int sections (const EqBand& b, int bandIndex, double fs, Biquad::Coeffs* out);

private:
    void glideAndUpdate() noexcept;
    template <typename T> void run (T* const* ch, int numCh, int n) noexcept;

    double fs = 48000.0;
    EqSettings target, current;
    std::array<std::array<std::array<Biquad, 4>, EqSettings::kBands>, 2> filt; // [channel][band][section]
    std::array<int, EqSettings::kBands> numSec {};
    Smoothed outGain;
    bool dirty = true;
};
} // namespace namvx
