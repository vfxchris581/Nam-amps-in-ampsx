#pragma once
// Harmonic enhancement that sits *after* the NAM capture and complements it.
// Three-band, dynamics-aware harmonic generation at the oversampled rate:
//   low band   (< 180 Hz)  - kept tight ("Low Definition" removes saturation smear down there)
//   mid band               - Chebyshev harmonic generator, even/odd balance control
//   high band  (> 3.2 kHz) - gentle "air" harmonics for pick and string detail
// Touch sensitivity scales the saturation with playing dynamics; a transient detector backs
// the harmonic mix off during pick attacks so the attack of the capture stays intact.
// The mix is level-matched and defaults low, so the capture's character is preserved.

#include "AudioEngine/DspCommon.h"

namespace namknobs
{
class HarmonicEngine
{
public:
    struct Settings
    {
        double amount = 0.25;        // 0..1 wet harmonics
        double evenOdd = 0.5;        // 0 = odd (symmetric) .. 1 = even (asymmetric)
        double touch = 0.5;          // 0..1 dynamics → saturation
        double attackPreserve = 0.6; // 0..1
        double lowDefinition = 0.5;  // 0..1 tightens lows
        double highDetail = 0.3;     // 0..1 high band harmonics
    };

    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings& s) noexcept { set = s; }
    double process (double x) noexcept;

    /** Static shaper (public for tests): asymmetric, unity slope at 0, even part set by 'bias'. */
    static double shape (double x, double drive, double bias) noexcept;

private:
    Settings set;
    double fs = 48000.0;
    Biquad lowLP1, lowLP2, highHP1, highHP2, tightHP;
    EnvelopeFollower envFast, envSlow, envLevel, envLow, envHigh;
    DcBlocker dc;
};
} // namespace namknobs
