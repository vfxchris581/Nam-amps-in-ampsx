#include "TriodeStage.h"

namespace namvx
{
double KorenTriode::plateCurrent (double vgk, double vpk) const noexcept
{
    if (vpk <= 0.0) return 0.0;
    const double arg = kp * (1.0 / mu + vgk / std::sqrt (kvb + vpk * vpk));
    // log(1+exp(a)) evaluated without overflow
    const double sp = arg > 30.0 ? arg : std::log1p (std::exp (arg));
    const double e1 = vpk / kp * sp;
    return e1 > 0.0 ? 2.0 * std::pow (e1, ex) / kg1 : 0.0; // Ip = E1^x / Kg1 · (1 + sgn E1)  [A]
}

TriodeStage::TriodeStage() { prepare (48000.0); }

void TriodeStage::prepare (double sampleRate)
{
    fs = sampleRate;
    dt = 1.0 / fs;

    // 1. Operating point: Vk = Rk·Ip(-Vk, Vp-Vk), Vp = B+ - Rp·Ip   (fixed-point iteration)
    double vk = 1.0, vp = 150.0, ip = 0.0;
    for (int it = 0; it < 200; ++it)
    {
        // inner solve of the plate for current vk (bisection on Vp)
        double lo = 0.0, hi = bPlus;
        for (int b = 0; b < 60; ++b)
        {
            const double mid = 0.5 * (lo + hi);
            const double f = mid - (bPlus - rp * tube.plateCurrent (-vk, mid - vk));
            (f > 0.0 ? hi : lo) = mid;
        }
        vp = 0.5 * (lo + hi);
        ip = tube.plateCurrent (-vk, vp - vk);
        vk = 0.5 * vk + 0.5 * rk * ip;
    }
    vk0 = vk; vp0 = vp; ip0 = ip;

    // 2. Static plate voltage vs vgk table, cathode held at its DC point.
    vpTable.assign ((size_t) kTable, 0.0);
    ipTable.assign ((size_t) kTable, 0.0);
    for (int i = 0; i < kTable; ++i)
    {
        const double vgk = kVgMin + (kVgMax - kVgMin) * i / (kTable - 1);
        double lo = 0.0, hi = bPlus;
        for (int b = 0; b < 60; ++b)
        {
            const double mid = 0.5 * (lo + hi);
            const double f = mid - (bPlus - rp * tube.plateCurrent (vgk, mid - vk0));
            (f > 0.0 ? hi : lo) = mid;
        }
        vpTable[(size_t) i] = 0.5 * (lo + hi);
        ipTable[(size_t) i] = (bPlus - vpTable[(size_t) i]) / rp;
    }

    // 3. Small-signal gain around the operating point (for level normalisation).
    const double h = 0.01;
    smallSignalGain = std::abs (lookupPlate (-vk0 + h) - lookupPlate (-vk0 - h)) / (2.0 * h);
    if (smallSignalGain < 1.0) smallSignalGain = 1.0;

    dc.prepare (fs, 8.0);
    reset();
}

void TriodeStage::reset()
{
    vk = vk0; vc = 0.0;
    dc.reset();
}

double TriodeStage::lookupPlate (double vgk) const noexcept
{
    const double pos = (std::clamp (vgk, kVgMin, kVgMax) - kVgMin) / (kVgMax - kVgMin) * (kTable - 1);
    const int i = std::min ((int) pos, kTable - 2);
    const double f = pos - i;
    return vpTable[(size_t) i] + f * (vpTable[(size_t) i + 1] - vpTable[(size_t) i]);
}

double TriodeStage::process (double x) noexcept
{
    // full scale → grid volts (exponential drive taper)
    const double inScale = 0.05 * std::pow (120.0, drive);
    const double vin = x * inScale;

    // Coupling cap + grid leak + grid conduction through the stopper.
    const double vgNode = vin - vc;
    const double over = vgNode - vk;                       // grid-cathode drive at the node
    // smooth diode: conduction rises over ~0.3 V
    const double cond = over > -2.0 ? 0.3 * std::log1p (std::exp (over / 0.3)) : 0.0;
    const double ig = cond / (rstop + rgk);
    const double vg = vgNode - ig * rstop;                 // voltage actually on the grid

    vc += dt * ((vgNode / rleak) + ig) / cc;               // cap charges → bias shift / blocking
    vc = flushDenormal (vc);

    const double vgk = vg - vk;
    const double pos = (std::clamp (vgk, kVgMin, kVgMax) - kVgMin) / (kVgMax - kVgMin) * (kTable - 1);
    const int i = std::min ((int) pos, kTable - 2);
    const double f = pos - i;
    const double vp = vpTable[(size_t) i] + f * (vpTable[(size_t) i + 1] - vpTable[(size_t) i]);
    const double ip = ipTable[(size_t) i] + f * (ipTable[(size_t) i + 1] - ipTable[(size_t) i]);

    // Cathode RC: bias follows average current (compression / sag feel)
    vk += dt * (ip - vk / rk) / ck;

    // Plate swing, inverted back to input polarity, normalised to unity small-signal gain.
    const double y = -(vp - vp0) / (smallSignalGain * inScale);
    return dc.process (y);
}
} // namespace namvx
