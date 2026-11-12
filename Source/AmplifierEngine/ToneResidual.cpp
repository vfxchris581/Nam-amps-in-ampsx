#include "ToneResidual.h"

namespace namvx
{
double ToneResidual::levelTaper (double knob) noexcept
{
    knob = std::clamp (knob, 0.0, 10.0);
    if (knob <= 0.001) return 0.0;
    return dbToGain (40.0 * std::log10 (knob / 5.0)); // 10 → +12 dB, 2.5 → -12 dB, 1 → -28 dB
}

void ToneResidual::prepare (double sampleRate, int)
{
    fs = sampleRate;
    for (auto* s : { &sBass, &sMid, &sTreble, &sPres, &sRes }) { s->prepare (fs / 64.0, 40.0); s->snap (0.0); }
    updateCoeffs();
    reset();
}

void ToneResidual::reset()
{
    bass.reset(); mid.reset(); treble.reset(); presence.reset(); resonance.reset();
}

void ToneResidual::setAmounts (const ResidualAmounts& r, bool inPA) noexcept
{
    prInPA = inPA;
    sBass.setTarget (std::clamp (r.bass, -10.0, 10.0) * kBassDb);
    sMid.setTarget (std::clamp (r.middle, -10.0, 10.0) * kMidDb);
    sTreble.setTarget (std::clamp (r.treble, -10.0, 10.0) * kTrebleDb);
    sPres.setTarget (inPA ? 0.0 : std::clamp (r.presence, -10.0, 10.0) * kPresenceDb);
    sRes.setTarget (inPA ? 0.0 : std::clamp (r.resonance, -10.0, 10.0) * kResonanceDb);
}

void ToneResidual::updateCoeffs() noexcept
{
    bass.setCoeffs (Biquad::lowShelf (fs, 140.0, 0.6, sBass.getCurrent()));
    mid.setCoeffs (Biquad::peak (fs, 650.0, 0.7, sMid.getCurrent()));
    treble.setCoeffs (Biquad::highShelf (fs, 2600.0, 0.6, sTreble.getCurrent()));
    presence.setCoeffs (Biquad::highShelf (fs, 4800.0, 0.7, sPres.getCurrent()));
    resonance.setCoeffs (Biquad::peak (fs, 90.0, 1.1, sRes.getCurrent()));
}

void ToneResidual::process (double* x, int n) noexcept
{
    // Coefficients glide in 64-sample steps (smoother time constant ~40 ms).
    for (int start = 0; start < n; start += 64)
    {
        bool changed = false;
        for (auto* s : { &sBass, &sMid, &sTreble, &sPres, &sRes })
            if (s->isSmoothing()) { s->next(); changed = true; }
        if (changed) updateCoeffs();

        const int end = std::min (n, start + 64);
        const bool any = sBass.getCurrent() != 0 || sMid.getCurrent() != 0 || sTreble.getCurrent() != 0
                         || sPres.getCurrent() != 0 || sRes.getCurrent() != 0;
        if (! any) continue;
        for (int i = start; i < end; ++i)
        {
            double v = x[i];
            v = resonance.process (bass.process (v));
            v = mid.process (v);
            v = presence.process (treble.process (v));
            x[i] = v;
        }
    }
}
} // namespace namvx
