#include "ParametricEQ.h"

#include <complex>

namespace namknobs
{
int ParametricEQ::sections (const EqBand& b, int idx, double sr, Biquad::Coeffs* out)
{
    const double f = std::clamp (b.freq, 10.0, 0.47 * sr);
    if (! b.on) return 0;
    switch (idx)
    {
        case EqSettings::LowCut:
        case EqSettings::HighCut:
        {
            // Butterworth cascades: 12 / 24 / 48 dB per octave
            static const double q2[] = { 0.70710678 };
            static const double q4[] = { 0.54119610, 1.30656296 };
            static const double q8[] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
            const double* qs = b.slope == 0 ? q2 : (b.slope == 1 ? q4 : q8);
            const int n = b.slope == 0 ? 1 : (b.slope == 1 ? 2 : 4);
            for (int i = 0; i < n; ++i)
                out[i] = idx == EqSettings::LowCut ? Biquad::highPass (sr, f, qs[i]) : Biquad::lowPass (sr, f, qs[i]);
            return n;
        }
        case EqSettings::LowShelf:  out[0] = Biquad::lowShelf (sr, f, std::max (0.1, b.q), b.gainDb); return 1;
        case EqSettings::HighShelf: out[0] = Biquad::highShelf (sr, f, std::max (0.1, b.q), b.gainDb); return 1;
        default:                    out[0] = Biquad::peak (sr, f, std::max (0.1, b.q), b.gainDb); return 1;
    }
}

double ParametricEQ::responseDb (const EqSettings& s, double sr, double f)
{
    if (! s.enabled) return 0.0;
    const std::complex<double> z1 = std::polar (1.0, -kTwoPi * f / sr);
    double db = s.outputDb;
    Biquad::Coeffs c[4];
    for (int b = 0; b < EqSettings::kBands; ++b)
    {
        const int n = sections (s.band[(size_t) b], b, sr, c);
        for (int i = 0; i < n; ++i)
        {
            const auto num = c[i].b0 + c[i].b1 * z1 + c[i].b2 * z1 * z1;
            const auto den = 1.0 + c[i].a1 * z1 + c[i].a2 * z1 * z1;
            db += 20.0 * std::log10 (std::max (1e-12, std::abs (num / den)));
        }
    }
    return db;
}

void ParametricEQ::prepare (double sampleRate)
{
    fs = sampleRate;
    current = target;
    outGain.prepare (fs, 20.0);
    outGain.snap (dbToGain (current.outputDb));
    dirty = true;
    glideAndUpdate();
    reset();
}

void ParametricEQ::reset()
{
    for (auto& ch : filt) for (auto& b : ch) for (auto& s : b) s.reset();
}

void ParametricEQ::glideAndUpdate() noexcept
{
    // glide current toward target (log frequency, linear gain/Q); ~25 ms at 32-sample steps
    const double a = 1.0 - std::exp (-32.0 / (0.025 * fs));
    for (int b = 0; b < EqSettings::kBands; ++b)
    {
        auto& c = current.band[(size_t) b];
        const auto& t = target.band[(size_t) b];
        if (c.on != t.on || c.slope != t.slope) { c = t; dirty = true; continue; } // structural change
        auto glide = [&] (double& v, double tv, bool logScale) {
            if (v == tv) return;
            if (logScale) v = std::exp (std::log (v) + (std::log (tv) - std::log (v)) * a);
            else v += (tv - v) * a;
            if (std::abs (v - tv) < (logScale ? tv * 1e-4 : 1e-4)) v = tv;
            dirty = true;
        };
        glide (c.freq, t.freq, true);
        glide (c.gainDb, t.gainDb, false);
        glide (c.q, t.q, false);
    }
    current.enabled = target.enabled;
    current.postCab = target.postCab;
    outGain.setTarget (target.enabled ? dbToGain (target.outputDb) : 1.0);
    if (! dirty) return;
    Biquad::Coeffs c[4];
    for (int b = 0; b < EqSettings::kBands; ++b)
    {
        numSec[(size_t) b] = sections (current.band[(size_t) b], b, fs, c);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSec[(size_t) b]; ++i) filt[(size_t) ch][(size_t) b][(size_t) i].setCoeffs (c[i]);
    }
    dirty = false;
}

template <typename T>
void ParametricEQ::run (T* const* chans, int numCh, int n) noexcept
{
    for (int start = 0; start < n; start += 32)
    {
        glideAndUpdate();
        const int end = std::min (n, start + 32);
        if (! current.enabled)
        {
            for (int i = start; i < end; ++i)
            {
                const double g = outGain.next();
                for (int ch = 0; ch < numCh; ++ch) chans[ch][i] = (T) (chans[ch][i] * g);
            }
            continue;
        }
        for (int i = start; i < end; ++i)
        {
            const double g = outGain.next();
            for (int ch = 0; ch < numCh; ++ch)
            {
                double v = (double) chans[ch][i];
                for (int b = 0; b < EqSettings::kBands; ++b)
                    for (int s = 0; s < numSec[(size_t) b]; ++s) v = filt[(size_t) ch][(size_t) b][(size_t) s].process (v);
                chans[ch][i] = (T) (v * g);
            }
        }
    }
}

void ParametricEQ::process (double* x, int n) noexcept
{
    double* ch[1] = { x };
    run (ch, 1, n);
}

void ParametricEQ::process (float* l, float* r, int n) noexcept
{
    float* ch[2] = { l, r };
    run (ch, 2, n);
}
} // namespace namknobs
