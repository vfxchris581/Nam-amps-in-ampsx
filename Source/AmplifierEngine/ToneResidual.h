#pragma once
// Realises the part of a knob move that the NAM captures cannot: controls that no capture
// varies, and knob positions beyond the captured range. The interpolator reports, per control,
// the coordinate its blend actually sits at; the difference to the knob is applied here with
// amp-like voicing (shelves/peaks at tone-stack frequencies), smoothed for automation.
// Volume/Master are level tapers; uncovered Gain pushes the capture's input harder.

#include <array>
#include "AudioEngine/DspCommon.h"
#include "NAMMetadata/CaptureMetadata.h"

namespace namknobs
{
struct ResidualAmounts
{
    // knob units (0..10 scale) still to be realised, positive = more
    double gain = 0, bass = 0, middle = 0, treble = 0, presence = 0, resonance = 0;
};

class ToneResidual
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** Per-block update. 'presenceResonanceInPowerAmp' routes those two to the power amp NFB. */
    void setAmounts (const ResidualAmounts& r, bool presenceResonanceInPowerAmp) noexcept;
    void process (double* x, int n) noexcept;

    /** Pre-NAM input gain implied by an uncovered/overshot gain knob (linear, smoothed by caller). */
    static double inputGainFor (double gainResidual) noexcept { return dbToGain (2.2 * gainResidual); }
    /** Partial level make-up so extra drive doesn't just mean "louder". */
    static double makeupFor (double gainResidual) noexcept { return dbToGain (-1.2 * gainResidual); }

    /** Audio-taper level for Volume / Master knobs: 0 → -inf, 5 → 0 dB, 10 → +12 dB. */
    static double levelTaper (double knob) noexcept;

    // dB per knob unit
    static constexpr double kBassDb = 1.3, kMidDb = 1.1, kTrebleDb = 1.2, kPresenceDb = 1.1, kResonanceDb = 1.0;

private:
    void updateCoeffs() noexcept;
    double fs = 48000.0;
    Biquad bass, mid, treble, presence, resonance;
    Smoothed sBass, sMid, sTreble, sPres, sRes;
    bool prInPA = false;
};
} // namespace namknobs
