#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tst
{
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }

struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
struct Reg { Reg (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); } };

inline double rms (const std::vector<double>& x, size_t a = 0, size_t b = 0)
{
    if (b == 0) b = x.size();
    double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (1, b - a));
}
inline double db (double g) { return 20.0 * std::log10 (std::max (1e-15, g)); }

/** Amplitude of a sinusoid at 'f' (Hann window). */
inline double tone (const std::vector<double>& x, size_t from, size_t n, double f, double fs)
{
    double re = 0, im = 0, ws = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2 * 3.14159265358979323846 * i / n);
        re += x[from + i] * w * std::cos (2 * 3.14159265358979323846 * f * i / fs);
        im -= x[from + i] * w * std::sin (2 * 3.14159265358979323846 * f * i / fs);
        ws += w;
    }
    return 2 * std::sqrt (re * re + im * im) / ws;
}
inline std::vector<double> sine (double f, double fs, size_t n, double amp)
{
    std::vector<double> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = amp * std::sin (2 * 3.14159265358979323846 * f * i / fs);
    return v;
}
inline bool allFinite (const std::vector<double>& v)
{
    for (double x : v) if (! std::isfinite (x)) return false;
    return true;
}
} // namespace tst

#define TEST(name) static void name(); static tst::Reg reg_##name (#name, name); static void name()
#define CHECK(cond) do { ++tst::checks(); if (! (cond)) { ++tst::failures(); \
    std::printf ("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, ...) do { ++tst::checks(); if (! (cond)) { ++tst::failures(); \
    std::printf ("    FAIL %s:%d  %s  — ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
#define INFO(...) do { std::printf ("      "); std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)
