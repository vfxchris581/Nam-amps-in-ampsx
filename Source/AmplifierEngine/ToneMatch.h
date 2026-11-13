#pragma once
// Tone transfer for blend amps: shapes the sound of the "gain source" capture(s) so that each
// frequency band takes on the tonal balance of whichever captures the user picked for that band
// (e.g. bass of capture B, mids of capture C) — one phase-coherent signal, no crossover summing.
//
// Bands follow the analyser: 0 resonance (60-120 Hz), 1 bass (120-300), 2 middle (300-1.5k),
// 3 treble (1.5k-4k), 4 presence (4k-8k). Five EQ sections are solved jointly (5x5 linear system
// on their combined response at the band centres) so the measured band levels land on target.

#include <array>
#include "AudioEngine/DspCommon.h"

namespace namknobs
{
class ToneMatch
{
public:
    static constexpr int kBands = 5;
    using Bands = std::array<double, kBands>;

    void prepare (double sampleRate);
    void reset();

    /** Desired change per band in dB (target balance minus the gain source's own balance). */
    void setCorrectionDb (const Bands& db) noexcept;
    void process (double* x, int n) noexcept;

    /** Solved section gains (dB) currently applied — for display and tests. */
    const Bands& getSectionGains() const noexcept { return section; }

    /** Response (dB) of the five sections with the given gains at frequency f. */
    double responseDb (const Bands& gains, double f) const noexcept;

    static constexpr double kCentres[kBands] = { 85.0, 190.0, 700.0, 2500.0, 5800.0 };

private:
    void solve (const Bands& wanted) noexcept;
    void applyCoeffs() noexcept;
    static Biquad::Coeffs sectionCoeffs (int band, double fs, double gainDb);

    double fs = 48000.0;
    std::array<Biquad, kBands> filters;
    Bands target {}, current {}, section {};
    bool active = false;
};
} // namespace namknobs
