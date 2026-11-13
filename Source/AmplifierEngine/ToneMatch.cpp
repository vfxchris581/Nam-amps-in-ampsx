#include "ToneMatch.h"

#include <algorithm>
#include <complex>

namespace namknobs
{
Biquad::Coeffs ToneMatch::sectionCoeffs (int band, double sr, double g)
{
    switch (band)
    {
        case 0:  return Biquad::peak (sr, kCentres[0], 1.1, g);
        case 1:  return Biquad::peak (sr, kCentres[1], 1.0, g);
        case 2:  return Biquad::peak (sr, kCentres[2], 0.6, g);
        case 3:  return Biquad::peak (sr, kCentres[3], 0.8, g);
        default: return Biquad::highShelf (sr, 4500.0, 0.7, g);
    }
}

double ToneMatch::responseDb (const Bands& gains, double f) const noexcept
{
    const std::complex<double> z = std::polar (1.0, -kTwoPi * f / fs); // z^-1
    double db = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        if (gains[(size_t) b] == 0.0) continue;
        const auto k = sectionCoeffs (b, fs, gains[(size_t) b]);
        const auto num = k.b0 + k.b1 * z + k.b2 * z * z;
        const auto den = 1.0 + k.a1 * z + k.a2 * z * z;
        db += 20.0 * std::log10 (std::abs (num / den));
    }
    return db;
}

void ToneMatch::prepare (double sampleRate)
{
    fs = sampleRate;
    section = {};
    current = {};
    target = {};
    applyCoeffs();
    reset();
}

void ToneMatch::reset()
{
    for (auto& f : filters) f.reset();
}

void ToneMatch::solve (const Bands& wanted) noexcept
{
    // Interaction matrix: A[i][j] = response at band i of section j per +1 dB (linearised at 6 dB).
    double A[kBands][kBands + 1];
    for (int j = 0; j < kBands; ++j)
    {
        Bands unit {};
        unit[(size_t) j] = 6.0;
        for (int i = 0; i < kBands; ++i) A[i][j] = responseDb (unit, kCentres[i]) / 6.0;
    }
    for (int i = 0; i < kBands; ++i) A[i][kBands] = wanted[(size_t) i];

    // Gaussian elimination with partial pivoting.
    for (int c = 0; c < kBands; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < kBands; ++r) if (std::abs (A[r][c]) > std::abs (A[piv][c])) piv = r;
        for (int k = 0; k <= kBands; ++k) std::swap (A[c][k], A[piv][k]);
        if (std::abs (A[c][c]) < 1e-9) continue;
        for (int r = 0; r < kBands; ++r)
        {
            if (r == c) continue;
            const double f = A[r][c] / A[c][c];
            for (int k = c; k <= kBands; ++k) A[r][k] -= f * A[c][k];
        }
    }
    for (int i = 0; i < kBands; ++i)
        section[(size_t) i] = std::abs (A[i][i]) > 1e-9 ? std::clamp (A[i][kBands] / A[i][i], -15.0, 15.0) : 0.0;

    // One refinement pass on the real (non-linear in dB) response.
    for (int it = 0; it < 2; ++it)
        for (int i = 0; i < kBands; ++i)
        {
            const double err = wanted[(size_t) i] - responseDb (section, kCentres[i]);
            section[(size_t) i] = std::clamp (section[(size_t) i] + 0.8 * err / std::max (0.3, A[i][i]), -15.0, 15.0);
        }
}

void ToneMatch::applyCoeffs() noexcept
{
    active = false;
    for (int b = 0; b < kBands; ++b)
    {
        filters[(size_t) b].setCoeffs (sectionCoeffs (b, fs, section[(size_t) b]));
        if (std::abs (section[(size_t) b]) > 0.01) active = true;
    }
}

void ToneMatch::setCorrectionDb (const Bands& db) noexcept
{
    for (int b = 0; b < kBands; ++b) target[(size_t) b] = std::clamp (db[(size_t) b], -12.0, 12.0);
}

void ToneMatch::process (double* x, int n) noexcept
{
    // Glide toward the target in 64-sample steps (~30 ms), re-solving only while moving.
    for (int start = 0; start < n; start += 64)
    {
        bool changed = false;
        for (int b = 0; b < kBands; ++b)
        {
            const double d = target[(size_t) b] - current[(size_t) b];
            if (d == 0.0) continue;
            current[(size_t) b] = std::abs (d) > 0.005 ? current[(size_t) b] + d * 0.15 : target[(size_t) b];
            changed = true;
        }
        if (changed)
        {
            solve (current);
            applyCoeffs();
        }
        if (! active) continue;
        const int end = std::min (n, start + 64);
        for (int i = start; i < end; ++i)
        {
            double v = x[i];
            for (auto& f : filters) v = f.process (v);
            x[i] = v;
        }
    }
}
} // namespace namknobs
