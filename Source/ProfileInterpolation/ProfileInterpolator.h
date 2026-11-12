#pragma once
// Multidimensional interpolation over the knob space of one amp channel.
//
// Each capture is a point in an 8-D knob space (unknown coordinates allowed). For a knob
// position the interpolator returns a small set of capture weights that
//   * sum to one,
//   * equal exactly 1 for a capture when the knobs sit on its settings,
//   * vary continuously with the knobs — including when the set of active captures changes
//     (truncated Shepard weights: w_i = max(0, d_i^-p - d_(K+1)^-p)), so sweeping a knob
//     never produces a jump,
//   * only consider dimensions that the captures actually span ("covered" controls).
// Controls the captures don't cover — or knob positions beyond the captured range — are
// reported back as residual offsets for the DSP stages to realise (see ToneResidual).

#include <algorithm>
#include <array>
#include <vector>
#include "NAMMetadata/CaptureMetadata.h"

namespace namvx
{
struct InterpolationResult
{
    static constexpr int kMaxActive = 6;
    int numActive = 0;
    std::array<int, kMaxActive> index {};
    std::array<double, kMaxActive> weight {};
    KnobCoords effective {};          // weighted coordinates of the blend (NaN where uncovered)
    std::array<bool, kNumControls> covered {};
};

class ProfileInterpolator
{
public:
    void setCaptures (const std::vector<KnobCoords>& coords, const std::vector<bool>& enabled);
    void setMaxActive (int k) noexcept { maxActive = std::clamp (k, 1, InterpolationResult::kMaxActive); }
    void setPower (double p) noexcept { power = std::max (1.0, p); }

    InterpolationResult compute (const KnobCoords& knobs) const;

    bool isCovered (Control c) const noexcept { return covered[(size_t) c]; }
    int numCaptures() const noexcept { return (int) points.size(); }

private:
    struct Point { KnobCoords c; bool enabled; int originalIndex; };
    std::vector<Point> points;
    std::array<bool, kNumControls> covered {};
    std::array<double, kNumControls> range {}, lo {}, hi {};
    std::array<double, kNumControls> fallback {}; // median, used where a capture lacks a covered coord
    struct DI { double d; int i; };
    mutable std::vector<DI> scratch; // preallocated: compute() is called on the audio thread
    int maxActive = 3;
    double power = 2.0;
};
} // namespace namvx
