#pragma once
// Audio-rate realisation of an interpolation result: runs the active NAM captures in parallel
// and mixes their *outputs* with per-sample weight glides. Output-domain blending keeps every
// capture's own dynamics, sag and pick response intact (dynamic response preservation).
//
//  * level match            - every capture brought to the same loudness (switchable live)
//  * phase handling (3 modes, switchable live, click-free):
//      Off           - captures summed as they are
//      Time-aligned  - processing/reamp latency equalised (whole samples)
//      Phase-matched - measured against each other with a probe signal: sub-sample time
//                      alignment + polarity correction, so blends don't comb-filter or cancel
//  * per-capture polarity flip (user), glided so flipping never clicks
//  * artifact prevention    - weights glide (sum stays 1); a capture that just left the blend keeps
//                             running for a hold time so its state is fresh if it comes back

#include <vector>
#include "NAMEngine/NamModel.h"
#include "ProfileInterpolator.h"
#include "AudioEngine/DspCommon.h"

namespace namknobs
{
struct BlendSource
{
    NamModel* model = nullptr;
    double staticGain = 1.0;     // user trim
    double matchGain = 1.0;      // level match: brings this capture to the reference loudness
    double matchDb = 0.0;        // the same, in dB (display)
    int intrinsicLatency = 0;    // capture's own latency in host samples (from analysis)
    double phaseLag = 0.0;       // measured residual lag vs the reference capture (samples, fractional)
    int phasePolarity = 1;       // measured polarity vs the reference capture (probe)
    int analysisPolarity = 1;    // polarity from the impulse analysis (Time-aligned mode)
};

enum class PhaseMode : int { Off = 0, TimeAligned = 1, PhaseMatched = 2 };

/** Ring-buffer delay with 4-point Lagrange interpolation and a gliding delay time. */
class FracDelay
{
public:
    void prepare (int maxDelaySamples);
    void reset();
    void setTarget (double d) noexcept { target = std::clamp (d, 0.0, (double) size - 4.0); }
    void snap() noexcept { current = target; }
    double process (double x) noexcept;

private:
    std::vector<double> buf;
    int size = 8, w = 0;
    double target = 0.0, current = 0.0;
};

class CaptureBlender
{
public:
    void prepare (double sampleRate, int maxBlockSize, std::vector<BlendSource> sources,
                  double fadeMs = 45.0, double holdMs = 600.0);
    void reset();

    void setAlwaysOn (bool b) noexcept { alwaysOn = b; }
    /** Audio thread: new target weights (from ProfileInterpolator::compute or blend levels). */
    void setTargets (const InterpolationResult& r) noexcept;
    /** Audio thread: user polarity per source (+1 / -1). */
    void setUserPolarity (int source, int sign) noexcept;
    /** Audio thread: phase mode (delays glide over ~40 ms). */
    void setPhaseMode (PhaseMode m) noexcept;
    /** Audio thread: level match on/off (glides over 50 ms). */
    void setLevelMatch (bool on) noexcept { matchTarget = on ? 1.0 : 0.0; }
    /** Jump straight to the targets (used when a runtime is first built). */
    void snapToTargets() noexcept;
    /** Audio thread. */
    void process (const double* in, double* out, int n) noexcept;

    /** Latency added by this stage (resampling + alignment headroom), host samples. */
    int getLatency() const noexcept { return reportedLatency; }
    int numSources() const noexcept { return (int) src.size(); }
    int numRunning() const noexcept { return running; }
    double currentWeight (int i) const noexcept { return w[(size_t) i]; }
    PhaseMode getPhaseMode() const noexcept { return mode; }
    const BlendSource& source (int i) const noexcept { return src[(size_t) i]; }

private:
    double delayFor (size_t i, PhaseMode m) const noexcept;
    double signFor (size_t i) const noexcept;

    std::vector<BlendSource> src;
    std::vector<double> w, target, sign, signTarget;
    std::vector<int> userSign;
    std::vector<int> hold, totalLatency;
    std::vector<FracDelay> align;
    std::vector<double> tmp;
    double rampPerSample = 0.001, signRamp = 0.001, matchAmt = 1.0, matchTarget = 1.0, matchStep = 0.001;
    int holdSamples = 0, reportedLatency = 0, running = 0, maxTotal = 0, headroom = 0;
    PhaseMode mode = PhaseMode::PhaseMatched;
    bool alwaysOn = false;
};
} // namespace namknobs
