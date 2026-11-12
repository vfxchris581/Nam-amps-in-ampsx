#pragma once
// Audio-rate realisation of an interpolation result: runs the active NAM captures in parallel
// and mixes their *outputs* with per-sample weight ramps. Output-domain blending keeps every
// capture's own dynamics, sag and pick response intact (dynamic response preservation).
//
//  * loudness compensation  - every capture normalised to a common level before mixing
//  * phase-aware blending   - captures are latency-aligned (integer delay) and polarity-matched
//                             from the ProfileAnalyzer, so blends don't comb-filter or cancel
//  * artifact prevention    - weights glide smoothly (sum stays 1); a capture that just left the blend keeps
//                             running for a hold time so its state is fresh if it comes back

#include <vector>
#include "NAMEngine/NamModel.h"
#include "ProfileInterpolator.h"
#include "AudioEngine/DspCommon.h"

namespace namvx
{
struct BlendSource
{
    NamModel* model = nullptr;
    double staticGain = 1.0;     // loudness compensation * user trim * polarity
    int intrinsicLatency = 0;    // capture's own latency in host samples (from analysis)
};

class CaptureBlender
{
public:
    void prepare (double sampleRate, int maxBlockSize, std::vector<BlendSource> sources,
                  double fadeMs = 45.0, double holdMs = 600.0);
    void reset();

    void setAlwaysOn (bool b) noexcept { alwaysOn = b; }
    /** Audio thread: new target weights (from ProfileInterpolator::compute). */
    void setTargets (const InterpolationResult& r) noexcept;
    /** Jump straight to the targets (used when a runtime is first built). */
    void snapToTargets() noexcept { w = target; }
    /** Audio thread. */
    void process (const double* in, double* out, int n) noexcept;

    /** Latency added by this stage (resampling + alignment), host samples. */
    int getLatency() const noexcept { return reportedLatency; }
    int numSources() const noexcept { return (int) src.size(); }
    int numRunning() const noexcept { return running; }
    double currentWeight (int i) const noexcept { return w[(size_t) i]; }

private:
    std::vector<BlendSource> src;
    std::vector<double> w, target;
    std::vector<int> hold;
    std::vector<IntDelay> align;
    std::vector<double> tmp;
    double rampPerSample = 0.001;
    int holdSamples = 0, reportedLatency = 0, running = 0;
    bool alwaysOn = false;
};
} // namespace namvx
