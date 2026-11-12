#include "SincResampler.h"
#include <cstring>

namespace namified
{
namespace
{
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1.0e-14 * sum) break;
    }
    return sum;
}
} // namespace

void SincResampler::prepare (double inRate, double outRate, int maxInputBlock, int halfTaps)
{
    step = inRate / outRate;
    // Keep 5% guard band below the lower Nyquist to suppress images/aliases.
    cutoff = 0.95 * std::min (1.0, outRate / inRate);
    halfWidth = (double) halfTaps / cutoff;

    const double beta = 10.0; // Kaiser beta → ~100 dB sidelobes
    const int n = (int) std::ceil (halfWidth * kTableRes) + 2;
    table.assign ((size_t) n + 1, 0.0);
    const double i0b = besselI0 (beta);
    for (int i = 0; i <= n; ++i)
    {
        const double x = (double) i / kTableRes;
        if (x >= halfWidth) { table[(size_t) i] = 0.0; continue; }
        const double u = x / halfWidth;
        const double win = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - u * u))) / i0b;
        const double t = kPi * cutoff * x;
        const double sinc = x == 0.0 ? 1.0 : std::sin (t) / t;
        table[(size_t) i] = cutoff * sinc * win;
    }

    hist.assign ((size_t) (maxInputBlock + 4 * (int) std::ceil (halfWidth) + 16), 0.0);
    reset();
}

void SincResampler::reset()
{
    std::fill (hist.begin(), hist.end(), 0.0);
    // Pre-roll with silence so the first outputs are well defined.
    histCount = (size_t) std::ceil (halfWidth) + 1;
    pos = std::ceil (halfWidth);
}

double SincResampler::kernel (double x) const noexcept
{
    x = std::abs (x) * kTableRes;
    const size_t i = (size_t) x;
    if (i + 1 >= table.size()) return 0.0;
    const double f = x - (double) i;
    return table[i] + f * (table[i + 1] - table[i]);
}

void SincResampler::process (const double* in, int n, SampleFifo& out) noexcept
{
    if (histCount + (size_t) n > hist.size())
        n = (int) (hist.size() - histCount); // never happens when prepared with correct block size
    std::memcpy (hist.data() + histCount, in, sizeof (double) * (size_t) n);
    histCount += (size_t) n;

    const int hw = (int) std::ceil (halfWidth);
    while (pos + hw < (double) histCount - 1)
    {
        const int centre = (int) std::floor (pos);
        const double frac = pos - centre;
        double acc = 0.0;
        for (int k = -hw + 1; k <= hw; ++k)
        {
            const int idx = centre + k;
            if (idx < 0) continue;
            acc += hist[(size_t) idx] * kernel ((double) k - frac);
        }
        out.push (acc);
        pos += step;
    }

    // Discard history that can no longer be reached.
    const int keepFrom = std::max (0, (int) std::floor (pos) - hw - 1);
    if (keepFrom > 0)
    {
        std::memmove (hist.data(), hist.data() + keepFrom, sizeof (double) * (histCount - (size_t) keepFrom));
        histCount -= (size_t) keepFrom;
        pos -= keepFrom;
    }
}
} // namespace namified
