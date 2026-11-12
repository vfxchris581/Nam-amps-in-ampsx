#include "PowerAmpStage.h"

namespace namvx
{
double KorenPentode::plateCurrent (double vg1, double vg2) const noexcept
{
    if (vg2 <= 1.0) return 0.0;
    const double arg = kp * (1.0 / mu + vg1 / vg2);
    const double sp = arg > 30.0 ? arg : std::log1p (std::exp (arg));
    const double e1 = vg2 / kp * sp;
    return e1 > 0.0 ? 2.0 * std::pow (e1, ex) / kg1 : 0.0;
}

void PowerAmpStage::setTubeType (TubeType t) noexcept
{
    if (t == TubeType::EL34) { tube = { 11.0, 1.35, 650.0, 60.0 }; raa = 3400.0; }
    else                     { tube = { 8.7, 1.35, 1460.0, 48.0 }; raa = 4000.0; }
    updateQuiescent();
}

void PowerAmpStage::setBias (double b) noexcept
{
    vBias = -46.0 + 16.0 * std::clamp (b, 0.0, 1.0); // cold -46 V .. hot -30 V
    updateQuiescent();
}

void PowerAmpStage::updateQuiescent() noexcept
{
    const double vg2 = vNom * 0.9;
    iq = 2.0 * tube.plateCurrent (vBias, vg2);
    // transconductance of the pair around a small-signal point (both sides contribute)
    const double h = 0.5;
    const double gm = (tube.plateCurrent (vBias + h, vg2) - tube.plateCurrent (vBias - h, vg2)) / (2.0 * h);
    gmNorm = std::max (1.0e-5, 2.0 * gm * raa * 0.5);
}

void PowerAmpStage::setPresenceResonance (double presenceDb, double resonanceDb) noexcept
{
    presDb = std::clamp (presenceDb, -15.0, 15.0);
    resDb = std::clamp (resonanceDb, -15.0, 15.0);
}

void PowerAmpStage::updateFeedbackFilters() noexcept
{
    // Boosting presence/resonance = *removing* feedback in that band.
    fbPresence.setCoeffs (Biquad::highShelf (fs, 4200.0, 0.6, -presDb));
    fbResonance.setCoeffs (Biquad::peak (fs, 95.0, 0.9, -resDb));
    // Speaker interaction is damped by the feedback (higher damping factor).
    const double damping = 1.0 - 0.8 * (beta / 0.6);
    spkLow.setCoeffs (Biquad::peak (fs, 95.0, 1.6, speaker * 7.0 * damping));
    spkHigh.setCoeffs (Biquad::highShelf (fs, 2800.0, 0.5, speaker * 4.5 * damping));
    lastPresDb = presDb; lastResDb = resDb; lastBeta = beta; lastSpeaker = speaker;
}

void PowerAmpStage::prepare (double sampleRate)
{
    fs = sampleRate;
    dt = 1.0 / fs;
    sagAtt = std::exp (-1.0 / (0.004 * fs));
    sagRel = std::exp (-1.0 / (0.090 * fs));
    fluxLeak = kTwoPi * 3.0 * dt; // flux decays below ~3 Hz (winding resistance)
    dc.prepare (fs, 6.0);
    updateQuiescent();
    updateFeedbackFilters();
    reset();
}

void PowerAmpStage::reset()
{
    vc1 = vc2 = flux = fbState = 0.0;
    iAvg = iq;
    vSupply = vNom;
    fbPresence.reset(); fbResonance.reset(); spkLow.reset(); spkHigh.reset();
    dc.reset();
}

double PowerAmpStage::side (double v, double& vcSide) noexcept
{
    // coupling cap (0.1µF / 220k bias feed) charged by grid current above 0 V
    const double vg = v - vcSide;
    const double over = vg + vBias;
    const double ig = over > -1.5 ? 0.25 * std::log1p (std::exp (over / 0.25)) / 1500.0 : 0.0;
    vcSide += dt * (vg / 220e3 + ig) / 0.1e-6;
    vcSide = flushDenormal (vcSide);
    const double vgEff = vBias + vg - ig * 1500.0;
    return tube.plateCurrent (vgEff, vSupply * 0.9);
}

double PowerAmpStage::process (double x) noexcept
{
    if (presDb != lastPresDb || resDb != lastResDb || beta != lastBeta || speaker != lastSpeaker)
        updateFeedbackFilters();

    // Global negative feedback (one-sample loop delay at the oversampled rate).
    const double fb = fbResonance.process (fbPresence.process (fbState));
    const double u = (x - beta * fb) * (1.0 + beta);

    // Phase inverter → grid swing (drive taper 3 V .. 240 V peak per side for full scale)
    const double gridScale = 3.0 * std::pow (80.0, drive);
    const double grid = u * gridScale;
    const double imb = 1.0 + 0.12 * mismatch;
    const double i1 = side (grid * imb, vc1);
    const double i2 = side (-grid / imb, vc2);

    // Sag: supply droops with average current draw.
    const double itot = i1 + i2;
    const double c = itot > iAvg ? sagAtt : sagRel;
    iAvg = itot + c * (iAvg - itot);
    const double rSag = 40.0 + 420.0 * sag;
    vSupply = std::clamp (vNom - rSag * (iAvg - iq), 0.45 * vNom, vNom * 1.02);

    // Plate swing with knee limiting tracking the (sagging) supply.
    double vpp = (i1 - i2) * raa * 0.5;
    const double vlim = std::max (20.0, vSupply - 70.0);
    const double r = vpp / vlim;
    vpp = vlim * r / std::pow (1.0 + std::pow (std::abs (r), 4.0), 0.25);

    // Output transformer: flux integrates voltage; core non-linearity bites at low frequencies.
    flux += dt * vpp - fluxLeak * flux;
    const double fn = flux / (vNom * 0.004);                   // ~1.0 ≈ onset of saturation
    const double mag = xfmr * 0.18 * vNom * (fn * fn * fn) / (1.0 + std::abs (fn * fn * fn) * 0.2);
    const double vout = vpp - mag;

    // Normalise to unity small-signal gain.
    double y = vout / (gmNorm * gridScale);
    fbState = y;

    y = spkHigh.process (spkLow.process (y));
    return dc.process (y);
}
} // namespace namvx
