#include "LevelMatch.h"
#include <algorithm>
#include <cmath>
#include "AudioEngine/DspCommon.h"

namespace namknobs
{
std::vector<double> LevelMatch::testPhrase (double sr)
{
    const double noteLen = 0.3;
    std::vector<double> x ((size_t) std::lround (3 * noteLen * sr), 0.0);
    const std::vector<std::vector<double>> notes { { 82.41 }, { 110.0, 164.81 }, { 246.94 } };
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const size_t start = (size_t) std::lround (k * noteLen * sr), len = (size_t) std::lround (noteLen * sr);
        for (size_t i = 0; i < len && start + i < x.size(); ++i)
        {
            const double t = i / sr;
            const double env = std::min (1.0, t / 0.002) * std::exp (-4.0 * t); // fast pick, natural decay
            double v = 0.0;
            for (double f0 : notes[k])
                for (int h = 1; h <= 8; ++h)
                    if (f0 * h < 0.45 * sr) v += std::sin (kTwoPi * f0 * h * t + 0.7 * h) / h;
            x[start + i] = env * v / (double) notes[k].size();
        }
    }
    double pk = 0.0;
    for (double v : x) pk = std::max (pk, std::abs (v));
    const double scale = pk > 0.0 ? dbToGain (-9.0) / pk : 0.0;
    for (auto& v : x) v *= scale;
    return x;
}

double LevelMatch::loudnessDb (const std::vector<double>& x, double sr)
{
    Biquad shelf, hp; // BS.1770 K-weighting: head-related high shelf + RLB high-pass
    shelf.setCoeffs (Biquad::highShelf (sr, 1681.97, 0.7072, 4.0));
    hp.setCoeffs (Biquad::highPass (sr, 38.135, 0.5003));
    double ms = 0.0;
    size_t count = 0;
    const size_t skip = (size_t) (0.01 * sr); // filter settling
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double y = hp.process (shelf.process (x[i]));
        if (i >= skip) { ms += y * y; ++count; }
    }
    if (count == 0 || ! std::isfinite (ms) || ms <= 0.0) return -200.0;
    return 10.0 * std::log10 (ms / (double) count);
}

double LevelMatch::correctionDb (double loudness)
{
    return std::clamp (kReferenceDb - loudness, -kMaxCorrectionDb, kMaxCorrectionDb);
}
} // namespace namknobs
