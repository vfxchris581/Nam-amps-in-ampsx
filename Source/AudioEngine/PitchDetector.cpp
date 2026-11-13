#include "PitchDetector.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace namknobs
{
int PitchDetector::samplesNeeded (double sampleRate, double minHz)
{
    return (int) std::ceil (2.2 * sampleRate / minHz) + 64;
}

PitchDetector::Result PitchDetector::detect (const float* x, int n, double sr, double minHz, double maxHz)
{
    Result res;
    if (n < 256 || sr <= 0.0) return res;

    // level gate: nothing to tune below about -60 dBFS
    double ms = 0.0;
    for (int i = 0; i < n; ++i) ms += (double) x[i] * x[i];
    if (std::sqrt (ms / n) < 1.0e-3) return res;

    // ---- coarse search on a decimated copy -------------------------------------------------
    const int D = std::max (1, (int) std::floor (sr / 11000.0));
    const double srd = sr / D;
    const int m = n / D;
    std::vector<double> y ((size_t) m);
    double mean = 0.0;
    for (int i = 0; i < m; ++i)
    {
        double s = 0.0;
        for (int k = 0; k < D; ++k) s += x[i * D + k];
        y[(size_t) i] = s / D;
        mean += y[(size_t) i];
    }
    mean /= m;
    for (auto& v : y) v -= mean;

    const int maxLag = std::min (m / 2 - 1, (int) std::ceil (srd / minHz));
    const int minLag = std::max (2, (int) std::floor (srd / maxHz));
    if (maxLag <= minLag + 2) return res;
    const int W = m - maxLag - 1;
    if (W < 32) return res;

    std::vector<double> d ((size_t) maxLag + 2, 0.0), cm ((size_t) maxLag + 2, 1.0);
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double s = 0.0;
        for (int j = 0; j < W; ++j) { const double e = y[(size_t) j] - y[(size_t) (j + tau)]; s += e * e; }
        d[(size_t) tau] = s;
    }
    double run = 0.0;
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        run += d[(size_t) tau];
        cm[(size_t) tau] = run > 0.0 ? d[(size_t) tau] * tau / run : 1.0;
    }

    int best = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
        if (cm[(size_t) tau] < 0.15)
        {
            while (tau + 1 <= maxLag && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        best = minLag;
        for (int tau = minLag; tau <= maxLag; ++tau) if (cm[(size_t) tau] < cm[(size_t) best]) best = tau;
        if (cm[(size_t) best] > 0.35) return res; // not periodic enough
    }
    res.clarity = std::clamp (1.0 - cm[(size_t) best], 0.0, 1.0);

    // ---- refine on the full-rate signal ------------------------------------------------------
    const int centre = best * D;
    const int lo = std::max (2, centre - D - 2), hi = centre + D + 2;
    const int Wf = n - hi - 2;
    if (Wf < 64) { res.hz = srd / best; return res; }
    auto diff = [&] (int tau) {
        double s = 0.0;
        for (int j = 0; j < Wf; ++j) { const double e = (double) x[j] - x[j + tau]; s += e * e; }
        return s;
    };
    std::vector<double> df ((size_t) (hi - lo + 1));
    int bi = 0;
    for (int t = lo; t <= hi; ++t)
    {
        df[(size_t) (t - lo)] = diff (t);
        if (df[(size_t) (t - lo)] < df[(size_t) bi]) bi = t - lo;
    }
    double tau = lo + bi;
    if (bi > 0 && bi < (int) df.size() - 1)
    {
        const double a = df[(size_t) bi - 1], b = df[(size_t) bi], c = df[(size_t) bi + 1];
        const double den = a - 2.0 * b + c;
        if (den > 0.0) tau += 0.5 * (a - c) / den;
    }
    res.hz = sr / tau;
    return res;
}

PitchDetector::Note PitchDetector::noteFor (double hz, double a4)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    Note nt;
    if (hz <= 0.0) return nt;
    const double semis = 12.0 * std::log2 (hz / a4) + 69.0;
    nt.midi = (int) std::lround (semis);
    nt.cents = (semis - nt.midi) * 100.0;
    nt.name = names[((nt.midi % 12) + 12) % 12];
    nt.octave = nt.midi / 12 - 1;
    return nt;
}
} // namespace namknobs
