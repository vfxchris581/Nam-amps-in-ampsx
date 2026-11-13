#include "NoiseGate.h"

namespace namknobs
{
void NoiseGate::prepare (double sampleRate)
{
    fs = sampleRate;
    msCoef = 1.0 - std::exp (-1.0 / (0.010 * fs));
    attackStep = 1.0 / std::max (1.0, kAttackMs * 0.001 * fs);
    holdLen = (int) (kHoldMs * 0.001 * fs);
    keyHp.setCoeffs (Biquad::highPass (fs, 45.0, 0.7071)); // ignore DC / rumble in the key
    lastReleaseMs = -1.0;
    reset();
}

void NoiseGate::reset() noexcept
{
    ms = 0.0; g = 1.0; open = true; hold = holdLen;
    keyHp.reset();
}

void NoiseGate::setParams (bool enabled, double thresholdDb, double releaseMs) noexcept
{
    if (enabled && ! on) { g = 1.0; open = true; hold = holdLen; } // switching on never mutes a ringing note
    on = enabled;
    thrOpen = dbToGain (thresholdDb);
    thrClose = dbToGain (thresholdDb - kHysteresisDb);
    if (releaseMs != lastReleaseMs)
    {
        lastReleaseMs = releaseMs;
        releaseCoef = std::exp (std::log (0.001) / std::max (1.0, releaseMs * 0.001 * fs)); // -60 dB in releaseMs
    }
}

void NoiseGate::computeGain (const double* key, double* gain, int n) noexcept
{
    if (! on)
    {
        std::fill (gain, gain + n, 1.0);
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        const double k = keyHp.process (key[i]);
        ms = flushDenormal (ms + msCoef * (2.0 * k * k - ms)); // 2x: a sine reads its peak level
        const double env = std::sqrt (ms);

        if (env >= thrOpen) { open = true; hold = holdLen; }
        else if (env < thrClose)
        {
            if (hold > 0) --hold;
            else open = false;
        }

        if (open) g = std::min (1.0, g + attackStep);
        else      g = flushDenormal (g * releaseCoef);
        gain[i] = g;
    }
}
} // namespace namknobs
