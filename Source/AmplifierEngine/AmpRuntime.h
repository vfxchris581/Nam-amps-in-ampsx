#pragma once
// The playable form of an AmpDefinition: all NAM networks loaded and prepared at the host rate,
// one interpolator + blender per channel. Built off the audio thread, handed over lock-free.

#include <memory>
#include <string>
#include <vector>
#include "AmpDefinition.h"
#include "ToneResidual.h"
#include "ProfileInterpolation/CaptureBlender.h"

namespace namvx
{
struct RuntimeOptions
{
    int maxActiveCaptures = 3;
    double loudnessCompensation = 1.0; // 0..1
    bool alwaysOnModels = false;       // run every capture all the time (max smoothness, max CPU)
    double referenceLoudnessDb = -18.0;
};

class AmpRuntime
{
public:
    /** Loads + prepares everything. Captures that fail to load are skipped and reported. */
    static std::unique_ptr<AmpRuntime> build (const AmpDefinition& def, double sampleRate, int maxBlock,
                                              const RuntimeOptions& opts, std::vector<std::string>& errors);

    int numChannels() const noexcept { return (int) channels.size(); }
    const std::string& getAmpId() const noexcept { return ampId; }
    int getLatency() const noexcept { return latency; }

    /** Audio thread: updates the blend targets of a channel and returns what is left for the
        residual DSP. Cheap enough to call once per block. */
    ResidualAmounts setKnobs (int channel, const KnobCoords& knobs) noexcept;

    /** Audio thread: mono in → mono out through one channel's blended captures. */
    void process (int channel, const double* in, double* out, int n) noexcept;

    /** Blend amps: audio thread, sets the mix level (0..1) of each model; weights are normalised. */
    void setBlendLevels (int channel, const std::array<double, AmpDefinition::kMaxBlendModels>& levels) noexcept;
    bool isBlend() const noexcept { return blend; }

    /** Auto mode: the amp knobs drive the blend. Each knob sweeps across the loaded captures sorted
        by how much of that quality they have (Gain: distortion; EQ knobs: band level). All arrays are
        indexed by slot. 'varies' tells which controls the captures actually differ in (row 0 = gain,
        1..5 = resonance, bass, middle, treble, presence); the others are left to the DSP knobs. */
    struct AutoBlend
    {
        std::array<double, AmpDefinition::kMaxBlendModels> gain {};
        std::array<std::array<double, AmpDefinition::kMaxBlendModels>, 5> bands {};
        std::array<bool, 6> varies {};
    };
    AutoBlend autoBlend (int channel, const KnobCoords& knobs) const noexcept;

    /** Blend amps: per-band correction (dB) that gives the gain-source mix the tonal balance chosen
        per band. gainLevels = audio mix of the models; bandLevels[band][model] = source mix per band. */
    std::array<double, 5> toneCorrection (int channel, const std::array<double, AmpDefinition::kMaxBlendModels>& gainLevels,
                                          const std::array<std::array<double, AmpDefinition::kMaxBlendModels>, 5>& bandLevels) const noexcept;

    bool isCovered (int channel, Control c) const noexcept;
    int numRunningModels (int channel) const noexcept;

private:
    struct Channel
    {
        std::vector<std::unique_ptr<NamModel>> models;
        ProfileInterpolator interp;
        CaptureBlender blender;
        KnobCoords baseline {};   // value used for uncovered controls
        IntDelay pad;             // equalises latency across channels
        std::vector<std::array<double, 5>> bands; // analysed tonal balance per source (dB)
        std::vector<int> slot;                    // blends: Tone Matrix column of each source
        std::vector<double> gainFeature;          // distortion measure per source (analysis)
    };
    std::vector<Channel> channels;
    std::string ampId;
    int latency = 0;
    bool blend = false;
};
} // namespace namvx
