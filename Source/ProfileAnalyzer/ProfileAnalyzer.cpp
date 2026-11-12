#include "ProfileAnalyzer.h"

#include <algorithm>
#include <numeric>
#include <random>
#include "AudioEngine/DspCommon.h"

namespace namified
{
namespace
{
std::vector<double> run (NamModel& m, const std::vector<double>& in, int block)
{
    std::vector<double> out (in.size(), 0.0);
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - i);
        m.process (in.data() + i, out.data() + i, n);
    }
    return out;
}

double rms (const std::vector<double>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += x[i] * x[i];
    return std::sqrt (s / (double) std::max<size_t> (1, to - from));
}

/** Amplitude of an exact-bin sinusoid via a Hann-windowed DFT probe. */
double toneAmplitude (const std::vector<double>& x, size_t from, size_t n, double freq, double fs)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    const double w = kTwoPi * freq / fs;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) n);
        re += x[from + i] * win * std::cos (w * (double) i);
        im -= x[from + i] * win * std::sin (w * (double) i);
        wsum += win;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}
} // namespace

CaptureAnalysis ProfileAnalyzer::analyse (NamModel& model)
{
    CaptureAnalysis a;
    const double fs = model.getModelSampleRate();
    const int block = 512;
    model.prepare (fs, block); // run natively — no resampling during analysis
    a.sampleRate = fs;

    // 1. Settle, then small impulse → latency + polarity.
    {
        std::vector<double> sig ((size_t) 16384, 0.0);
        sig[8192] = 0.05;
        auto y = run (model, sig, block);
        double dc = 0.0;
        for (size_t i = 4096; i < 8192; ++i) dc += y[i];
        dc /= 4096.0;
        size_t peak = 8192; double best = 0.0;
        for (size_t i = 8192; i < 8192 + 4096; ++i)
            if (std::abs (y[i] - dc) > best) { best = std::abs (y[i] - dc); peak = i; }
        // onset = first sample reaching 50% of the peak (robust to slow resonant peaks)
        size_t onset = peak;
        for (size_t i = 8192; i <= peak; ++i)
            if (std::abs (y[i] - dc) >= 0.5 * best) { onset = i; break; }
        a.latencySamples = (int) (onset - 8192);
        a.polarity = (y[peak] - dc) >= 0.0 ? 1 : -1;
    }

    // 2. Level sweep with a 220 Hz sine: compression and harmonic content.
    {
        const double f0 = 220.0;
        const size_t seg = (size_t) (0.30 * fs), meas = (size_t) (0.15 * fs);
        const double levelsDb[] = { -36.0, -24.0, -12.0, -6.0 };
        double outDb[4] {};
        for (int L = 0; L < 4; ++L)
        {
            std::vector<double> s (seg);
            const double amp = dbToGain (levelsDb[L]) * std::sqrt (2.0);
            for (size_t i = 0; i < seg; ++i) s[i] = amp * std::sin (kTwoPi * f0 * (double) i / fs);
            auto y = run (model, s, block);
            outDb[L] = gainToDb (rms (y, seg - meas, seg));

            if (L == 2)
            {
                // measure an integer number of periods for clean harmonic probes
                const size_t n = (size_t) (std::floor ((double) meas * f0 / fs) * fs / f0);
                const double h1 = toneAmplitude (y, seg - n, n, f0, fs);
                double even = 0.0, odd = 0.0;
                for (int k = 2; k <= 12; ++k)
                {
                    if (f0 * k >= 0.45 * fs) break;
                    const double hk = toneAmplitude (y, seg - n, n, f0 * k, fs);
                    (k % 2 == 0 ? even : odd) += hk * hk;
                }
                a.thdDb = gainToDb (std::sqrt (even + odd) / std::max (1.0e-12, h1));
                a.evenOddDb = 10.0 * std::log10 ((even + 1.0e-20) / (odd + 1.0e-20));
            }
        }
        a.compression = (outDb[3] - outDb[0]) / 30.0;
    }

    // 3. Broadband multitone at -18 dBFS RMS: loudness + tonal balance.
    {
        const size_t N = 16384;
        const size_t settle = 8192;
        const int tones = 48;
        std::vector<double> freqs;
        for (int t = 0; t < tones; ++t)
        {
            const double f = 60.0 * std::pow (8000.0 / 60.0, (double) t / (tones - 1));
            const double bin = std::round (f * (double) N / fs);
            if (bin >= 1 && (freqs.empty() || bin * fs / (double) N > freqs.back() + 1.0))
                freqs.push_back (bin * fs / (double) N);
        }
        std::mt19937 rng (1234);
        std::uniform_real_distribution<double> ph (0.0, kTwoPi);
        std::vector<double> phases (freqs.size());
        for (auto& p : phases) p = ph (rng);

        std::vector<double> s (settle + N, 0.0);
        for (size_t i = 0; i < s.size(); ++i)
            for (size_t t = 0; t < freqs.size(); ++t)
                s[i] += std::sin (kTwoPi * freqs[t] * (double) i / fs + phases[t]);
        const double scale = dbToGain (-18.0) / std::max (1.0e-12, rms (s, 0, s.size()));
        for (auto& v : s) v *= scale;

        auto y = run (model, s, block);
        a.loudnessDb = gainToDb (rms (y, settle, settle + N));

        const double inAmp = scale; // each tone's input amplitude
        double bandSum[5] {}; int bandCount[5] {};
        const double edges[6] = { 60.0, 120.0, 300.0, 1500.0, 4000.0, 8200.0 };
        double total = 0.0; int totalN = 0;
        for (size_t t = 0; t < freqs.size(); ++t)
        {
            // Rectangular window is exact on bin-centred tones.
            double re = 0.0, im = 0.0;
            const double w = kTwoPi * freqs[t] / fs;
            for (size_t i = 0; i < N; ++i)
            {
                re += y[settle + i] * std::cos (w * (double) (settle + i));
                im -= y[settle + i] * std::sin (w * (double) (settle + i));
            }
            const double g = gainToDb (2.0 * std::sqrt (re * re + im * im) / (double) N / inAmp);
            total += g; ++totalN;
            for (int b = 0; b < 5; ++b)
                if (freqs[t] >= edges[b] && freqs[t] < edges[b + 1]) { bandSum[b] += g; ++bandCount[b]; }
        }
        const double avg = total / std::max (1, totalN);
        for (int b = 0; b < 5; ++b)
            a.bandsDb[(size_t) b] = bandCount[b] > 0 ? bandSum[b] / bandCount[b] - avg : 0.0;
    }

    a.valid = true;
    return a;
}

double ProfileAnalyzer::featureFor (Control c, const CaptureAnalysis& a) noexcept
{
    const auto& b = a.bandsDb; // 0 res, 1 bass, 2 mid, 3 treble, 4 presence
    switch (c)
    {
        case Control::Gain:      return a.thdDb - 20.0 * (a.compression - 1.0); // more THD + more compression
        case Control::Bass:      return b[1] - b[2];
        case Control::Middle:    return b[2] - 0.5 * (b[1] + b[3]);
        case Control::Treble:    return b[3] - b[2];
        case Control::Presence:  return b[4] - b[3];
        case Control::Resonance: return b[0] - b[1];
        default:                 return 0.0; // Volume/Master are level controls: not estimable from tone
    }
}

std::vector<std::array<bool, kNumControls>>
ProfileAnalyzer::estimateCoordinates (std::vector<KnobCoords>& coords, const std::vector<CaptureAnalysis>& analyses)
{
    const size_t n = coords.size();
    std::vector<std::array<bool, kNumControls>> estimated (n);
    for (auto& e : estimated) e.fill (false);
    if (n < 2 || analyses.size() != n) return estimated;

    const Control estimable[] = { Control::Gain, Control::Bass, Control::Middle, Control::Treble,
                                  Control::Presence, Control::Resonance };
    for (Control c : estimable)
    {
        const size_t ci = (size_t) c;
        std::vector<double> f (n);
        double fmin = 1e9, fmax = -1e9;
        for (size_t i = 0; i < n; ++i)
        {
            f[i] = analyses[i].valid ? featureFor (c, analyses[i]) : std::nan ("");
            if (isKnown (f[i])) { fmin = std::min (fmin, f[i]); fmax = std::max (fmax, f[i]); }
        }
        // Only claim a setting difference when it is clearly audible.
        const double minSpread = c == Control::Gain ? 4.0 : 1.5;
        if (! (fmax - fmin >= minSpread)) continue;

        // Linear fit against captures whose coordinate is known (if >= 2), else rank-map to 2..8.
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int k = 0;
        for (size_t i = 0; i < n; ++i)
            if (isKnown (coords[i][ci]) && isKnown (f[i]))
            { sx += f[i]; sy += coords[i][ci]; sxx += f[i] * f[i]; sxy += f[i] * coords[i][ci]; ++k; }

        double slope = 6.0 / (fmax - fmin), icpt = 2.0 - slope * fmin;
        if (k >= 2)
        {
            const double den = k * sxx - sx * sx;
            if (std::abs (den) > 1e-9)
            {
                const double s2 = (k * sxy - sx * sy) / den;
                if (s2 > 0.0) { slope = s2; icpt = (sy - s2 * sx) / k; }
            }
        }
        else if (k == 1) continue; // one reference point cannot calibrate a scale; leave for the user

        for (size_t i = 0; i < n; ++i)
            if (! isKnown (coords[i][ci]) && isKnown (f[i]))
            {
                coords[i][ci] = std::clamp (slope * f[i] + icpt, 0.0, 10.0);
                estimated[i][ci] = true;
            }
    }
    return estimated;
}
} // namespace namified
