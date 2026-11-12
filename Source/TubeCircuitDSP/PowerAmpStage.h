#pragma once
// Push-pull pentode power amp (EL34 / 6L6 Koren models) with:
//  * class-AB bias (cold → crossover distortion, hot → smoother, earlier compression)
//  * pair mismatch → controllable even-order content (perfect push-pull cancels it)
//  * grid conduction + coupling-cap bias shift per side (blocking distortion)
//  * plate "knee" limiting that tracks the supply voltage
//  * power-supply sag (rectifier + filter cap RC, current-dependent B+ and screen droop)
//  * output-transformer core saturation (flux integrator → low-frequency-dependent distortion)
//  * global negative feedback with presence / resonance shaping inside the loop
//  * speaker-impedance interaction (low resonance bump + voice-coil HF rise, damped by NFB)
// Runs at the oversampled rate.

#include "AudioEngine/DspCommon.h"

namespace namified
{
struct KorenPentode
{
    double mu = 11.0, ex = 1.35, kg1 = 650.0, kp = 60.0;  // EL34
    double plateCurrent (double vg1, double vg2) const noexcept;
};

class PowerAmpStage
{
public:
    enum class TubeType { EL34 = 0, SixL6 = 1 };

    void prepare (double sampleRate);
    void reset();

    void setTubeType (TubeType t) noexcept;
    void setDrive (double d) noexcept { drive = std::clamp (d, 0.0, 1.0); }     // 0..1
    void setBias (double b) noexcept;                                          // 0 cold .. 1 hot
    void setSag (double s) noexcept { sag = std::clamp (s, 0.0, 1.0); }
    void setMismatch (double m) noexcept { mismatch = std::clamp (m, 0.0, 1.0); }
    void setTransformer (double t) noexcept { xfmr = std::clamp (t, 0.0, 1.0); }
    void setFeedback (double f) noexcept { beta = 0.6 * std::clamp (f, 0.0, 1.0); }
    /** Presence / resonance in dB of boost realised by reducing feedback in that band. */
    void setPresenceResonance (double presenceDb, double resonanceDb) noexcept;
    void setSpeakerInteraction (double s) noexcept { speaker = std::clamp (s, 0.0, 1.0); }

    double process (double x) noexcept;
    double getSupplyVoltage() const noexcept { return vSupply; }

private:
    void updateQuiescent() noexcept;
    void updateFeedbackFilters() noexcept;
    double side (double v, double& vcSide) noexcept;

    KorenPentode tube;
    double fs = 48000.0, dt = 1.0 / 48000.0;
    double vNom = 450.0, vSupply = 450.0, vBias = -37.0, raa = 3400.0;
    double iq = 0.0, gmNorm = 1.0;
    double drive = 0.4, sag = 0.4, mismatch = 0.1, xfmr = 0.3, beta = 0.3, speaker = 0.5;
    double presDb = 0.0, resDb = 0.0, lastPresDb = 1e9, lastResDb = 1e9, lastBeta = -1.0, lastSpeaker = -1.0;

    double vc1 = 0.0, vc2 = 0.0, iAvg = 0.0, flux = 0.0, fbState = 0.0;
    double sagAtt = 0.0, sagRel = 0.0, fluxLeak = 0.0;
    Biquad fbPresence, fbResonance, spkLow, spkHigh;
    DcBlocker dc;
};
} // namespace namified
