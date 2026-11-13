#pragma once
// Noise gate for a high-gain amp.
//
// The detector listens to the guitar *before* the amp (where the playing is far above the hiss),
// and the gain is applied *after* the amp, so the amp's own amplified hiss is removed too and
// the cabinet tail rings out naturally. Because the amp path has a few milliseconds of latency,
// the gate opens slightly before the note reaches it: picks are never chopped.
//
//  * RMS level detector (10 ms), scaled so a sine reads its peak: noise is judged by its
//    average level, not by its random peaks, so the gate doesn't chatter on hiss
//  * 6 dB hysteresis + 25 ms hold (no chatter around the threshold)
//  * 0.5 ms attack, release = time for the gain to fall by 60 dB

#include "DspCommon.h"

namespace namknobs
{
class NoiseGate
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (bool enabled, double thresholdDb, double releaseMs) noexcept;

    /** key = guitar signal before the amp; writes one gain (0..1) per sample. */
    void computeGain (const double* key, double* gain, int n) noexcept;

    double currentGain() const noexcept { return g; }
    bool isEnabled() const noexcept { return on; }

    static constexpr double kHysteresisDb = 6.0, kHoldMs = 25.0, kAttackMs = 0.5;

private:
    double fs = 48000.0;
    bool on = false, open = true;
    double thrOpen = 0.0, thrClose = 0.0, msCoef = 0.0, attackStep = 1.0, releaseCoef = 0.0;
    double ms = 0.0, g = 1.0, lastReleaseMs = -1.0;
    int hold = 0, holdLen = 0;
    Biquad keyHp;
};
} // namespace namknobs
