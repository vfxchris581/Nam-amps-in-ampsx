#pragma once
// Small, allocation-free DSP building blocks shared by every Namified module.
// All state is double precision (64-bit internal processing).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace namified
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g, double floorDb = -150.0) noexcept
{
    return g > 0.0 ? std::max (floorDb, 20.0 * std::log10 (g)) : floorDb;
}

inline double flushDenormal (double x) noexcept { return std::abs (x) < 1.0e-30 ? 0.0 : x; }

/** Exponential smoother used for click-free parameter changes. */
class Smoothed
{
public:
    void prepare (double sampleRate, double timeMs) noexcept
    {
        coeff = timeMs <= 0.0 ? 0.0 : std::exp (-1.0 / (0.001 * timeMs * sampleRate));
    }
    void setTarget (double t) noexcept { target = t; }
    void snap (double v) noexcept { target = current = v; }
    double next() noexcept
    {
        current = target + (current - target) * coeff;
        if (std::abs (current - target) < 1.0e-9) current = target;
        return current;
    }
    double getCurrent() const noexcept { return current; }
    double getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return current != target; }

private:
    double coeff = 0.0, target = 0.0, current = 0.0;
};

/** RBJ biquad, transposed direct form II, double precision. */
class Biquad
{
public:
    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    static Coeffs lowShelf (double fs, double f0, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = kTwoPi * f0 / fs;
        const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q), sA = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) + (A - 1) * cw + sA;
        return { A * ((A + 1) - (A - 1) * cw + sA) / a0, 2 * A * ((A - 1) - (A + 1) * cw) / a0,
                 A * ((A + 1) - (A - 1) * cw - sA) / a0, -2 * ((A - 1) + (A + 1) * cw) / a0,
                 ((A + 1) + (A - 1) * cw - sA) / a0 };
    }
    static Coeffs highShelf (double fs, double f0, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = kTwoPi * f0 / fs;
        const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q), sA = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) - (A - 1) * cw + sA;
        return { A * ((A + 1) + (A - 1) * cw + sA) / a0, -2 * A * ((A - 1) + (A + 1) * cw) / a0,
                 A * ((A + 1) + (A - 1) * cw - sA) / a0, 2 * ((A - 1) - (A + 1) * cw) / a0,
                 ((A + 1) - (A - 1) * cw - sA) / a0 };
    }
    static Coeffs peak (double fs, double f0, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = kTwoPi * f0 / fs;
        const double alpha = std::sin (w) / (2.0 * q), cw = std::cos (w);
        const double a0 = 1 + alpha / A;
        return { (1 + alpha * A) / a0, -2 * cw / a0, (1 - alpha * A) / a0, -2 * cw / a0, (1 - alpha / A) / a0 };
    }
    static Coeffs lowPass (double fs, double f0, double q)
    {
        const double w = kTwoPi * f0 / fs, alpha = std::sin (w) / (2.0 * q), cw = std::cos (w);
        const double a0 = 1 + alpha;
        return { (1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0, -2 * cw / a0, (1 - alpha) / a0 };
    }
    static Coeffs highPass (double fs, double f0, double q)
    {
        const double w = kTwoPi * f0 / fs, alpha = std::sin (w) / (2.0 * q), cw = std::cos (w);
        const double a0 = 1 + alpha;
        return { (1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - alpha) / a0 };
    }

    void setCoeffs (const Coeffs& c) noexcept { k = c; }
    void reset() noexcept { z1 = z2 = 0.0; }
    double process (double x) noexcept
    {
        const double y = k.b0 * x + z1;
        z1 = flushDenormal (k.b1 * x - k.a1 * y + z2);
        z2 = flushDenormal (k.b2 * x - k.a2 * y);
        return y;
    }

private:
    Coeffs k;
    double z1 = 0.0, z2 = 0.0;
};

/** One-pole lowpass (also used as a leaky integrator / envelope smoother). */
class OnePole
{
public:
    void setCutoff (double fs, double fc) noexcept { a = std::exp (-kTwoPi * fc / fs); }
    void setTimeMs (double fs, double ms) noexcept { a = std::exp (-1.0 / (0.001 * ms * fs)); }
    double process (double x) noexcept { s = flushDenormal (x + a * (s - x)); return s; }
    void reset (double v = 0.0) noexcept { s = v; }
    double get() const noexcept { return s; }

private:
    double a = 0.0, s = 0.0;
};

/** First-order DC blocker. */
class DcBlocker
{
public:
    void prepare (double fs, double fc = 7.0) noexcept { r = std::exp (-kTwoPi * fc / fs); }
    void reset() noexcept { x1 = y1 = 0.0; }
    double process (double x) noexcept
    {
        const double y = x - x1 + r * y1;
        x1 = x; y1 = flushDenormal (y);
        return y;
    }

private:
    double r = 0.999, x1 = 0.0, y1 = 0.0;
};

/** Attack/release envelope follower on |x|. */
class EnvelopeFollower
{
public:
    void prepare (double fs, double attackMs, double releaseMs) noexcept
    {
        att = std::exp (-1.0 / (0.001 * attackMs * fs));
        rel = std::exp (-1.0 / (0.001 * releaseMs * fs));
    }
    double process (double x) noexcept
    {
        const double v = std::abs (x);
        const double c = v > env ? att : rel;
        env = flushDenormal (v + c * (env - v));
        return env;
    }
    void reset() noexcept { env = 0.0; }
    double get() const noexcept { return env; }

private:
    double att = 0.0, rel = 0.0, env = 0.0;
};

/** Integer delay line with preallocated storage (no allocation once prepared). */
class IntDelay
{
public:
    void prepare (int maxDelay) { buf.assign ((size_t) std::max (1, maxDelay + 1), 0.0); w = 0; }
    void setDelay (int d) noexcept { delay = std::clamp (d, 0, (int) buf.size() - 1); }
    int getDelay() const noexcept { return delay; }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0); }
    double process (double x) noexcept
    {
        if (delay == 0) return x;
        const size_t n = buf.size();
        buf[(size_t) w] = x;
        const size_t r = ((size_t) w + n - (size_t) delay) % n;
        w = (int) (((size_t) w + 1) % n);
        return buf[r];
    }

private:
    std::vector<double> buf;
    int w = 0, delay = 0;
};

/** Fixed-capacity FIFO of doubles (single thread; audio-safe once allocated). */
class SampleFifo
{
public:
    void allocate (size_t capacity) { data.assign (capacity, 0.0); clear(); }
    void clear() noexcept { r = w = count = 0; }
    size_t size() const noexcept { return count; }
    size_t free() const noexcept { return data.size() - count; }
    void push (double v) noexcept
    {
        if (count == data.size()) return; // overflow: drop (never happens with correct sizing)
        data[w] = v; w = (w + 1) % data.size(); ++count;
    }
    double pop() noexcept
    {
        if (count == 0) return 0.0;
        const double v = data[r]; r = (r + 1) % data.size(); --count;
        return v;
    }

private:
    std::vector<double> data;
    size_t r = 0, w = 0, count = 0;
};
} // namespace namified
