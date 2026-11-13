#pragma once
// 12AX7 common-cathode gain stage, solved from the Koren triode equations.
//
//  input ──Cc 22nF──┬──Rs 68k── grid ─┐
//                   Rleak 1M          12AX7 ── plate ── Rp 100k ── B+ 250 V
//                   │                 cathode ── Rk 1.5k || Ck 22µF
//
// Modelled behaviour:
//  * exact static plate transfer (load-line solve of the Koren model, tabulated)
//  * grid conduction through the grid stopper (soft positive-side limiting)
//  * coupling-cap charging by grid current → blocking / bias shift on hard transients
//  * cathode-bypass dynamics → bias drift with signal level (tube compression & sag feel)
//  * naturally asymmetric → even-order harmonics at moderate drive, odd at heavy drive
// Runs at the oversampled rate (the caller oversamples).

#include <vector>
#include "AudioEngine/DspCommon.h"

namespace namknobs
{
struct KorenTriode
{
    double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0; // 12AX7 (Koren)
    /** Plate current in amps. */
    double plateCurrent (double vgk, double vpk) const noexcept;
};

class TriodeStage
{
public:
    TriodeStage();
    void prepare (double sampleRate);
    void reset();

    /** drive: 0..1 maps the full-scale input to 0.05 .. 6 V peak at the grid. */
    void setDrive (double d) noexcept { drive = std::clamp (d, 0.0, 1.0); }
    double process (double x) noexcept;

    double getOperatingPlateVoltage() const noexcept { return vp0; }
    double getSmallSignalGain() const noexcept { return smallSignalGain; }

private:
    double lookupPlate (double vgk) const noexcept;

    KorenTriode tube;
    double bPlus = 250.0, rp = 100e3, rk = 1.5e3, ck = 22e-6, cc = 22e-9, rleak = 1e6, rstop = 68e3, rgk = 1.5e3;
    double fs = 48000.0, dt = 1.0 / 48000.0;

    // static load-line table over vgk
    static constexpr double kVgMin = -12.0, kVgMax = 4.0;
    static constexpr int kTable = 4096;
    std::vector<double> vpTable, ipTable;

    double vk0 = 1.0, vp0 = 150.0, ip0 = 0.0;
    double vk = 1.0, vc = 0.0;
    double drive = 0.3, smallSignalGain = 50.0;
    DcBlocker dc;
};
} // namespace namknobs
