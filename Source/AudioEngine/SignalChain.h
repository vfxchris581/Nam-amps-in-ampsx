#pragma once
// The complete Nam VX signal path:
//
//  in → input gain → [OS] 12AX7 stage [/OS] → gain residual → NAM capture blend (channel xfade)
//     → make-up · volume → tone residual EQ → [OS] harmonic engine → power amp (master) [/OS]
//     → dual-IR cabinet → output gain → out (stereo)
//
// 64-bit processing up to the cabinet; oversampling (2x/4x/8x, IIR low-latency or FIR linear
// phase) around every nonlinearity that is not a NAM capture; denormals flushed; every level
// and drive smoothed; amp/channel/preset changes crossfade.

#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include "EngineParameters.h"
#include "RealtimeSwap.h"
#include "AmplifierEngine/AmpRuntime.h"
#include "AmplifierEngine/ToneResidual.h"
#include "AmplifierEngine/ToneMatch.h"
#include "TubeCircuitDSP/TriodeStage.h"
#include "TubeCircuitDSP/PowerAmpStage.h"
#include "HarmonicEngine/HarmonicEngine.h"
#include "Convolution/DualCabinet.h"

namespace namvx
{
class SignalChain
{
public:
    enum class OversamplingQuality { X2 = 1, X4 = 2, X8 = 3 };

    SignalChain();
    ~SignalChain();

    /** Non-realtime. */
    void prepare (double sampleRate, int maxBlockSize);
    /** Non-realtime (suspend processing around it). */
    void setOversampling (OversamplingQuality q, bool linearPhase);

    /** Non-realtime: hand a new amp to the audio thread (crossfaded in). nullptr = no amp. */
    void publishRuntime (std::unique_ptr<AmpRuntime> rt);
    void collectGarbage() { runtimes.collectGarbage(); }

    /** Audio thread, once per block. */
    void setParameters (const EngineParameters& p) noexcept { params = p; }
    /** Audio thread / any: a short fade-dip that masks the jump of a preset change. */
    void requestDip() noexcept { dipRequested = true; }

    /** Audio thread. Reads channel 0 (guitar), writes all output channels (1 or 2). */
    void process (float* const* io, int numInputChannels, int numOutputChannels, int n) noexcept;
    /** Audio thread, double-precision host path. */
    void process (double* const* io, int numInputChannels, int numOutputChannels, int n) noexcept;

    DualCabinet& getCabinet() noexcept { return cabinet; }
    int getLatencySamples() const noexcept { return latency.load(); }
    double getSampleRate() const noexcept { return sampleRate; }

    // metering / UI feedback (relaxed atomics)
    std::atomic<float> inputPeak { 0 }, outputPeak { 0 };
    std::atomic<int> runningModels { 0 };
    std::atomic<bool> hasAmp { false };
    std::atomic<int> currentRuntimeChannels { 0 };
    std::array<std::atomic<float>, 5> toneCorrectionDb {}; // blend tone transfer per band (UI read-out)

private:
    void processMono (double* buf, int n) noexcept;
    void updateLatency() noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 512;
    OversamplingQuality osQuality = OversamplingQuality::X4;
    bool osLinearPhase = false;

    EngineParameters params;
    RealtimeSwap<AmpRuntime> runtimes;
    AmpRuntime* fadingOut = nullptr;
    int ampFade = 0, ampFadeLen = 1;
    int lastChannel = 0, prevChannel = -1, chanFade = 0, chanFadeLen = 1;

    std::unique_ptr<juce::dsp::Oversampling<double>> osPre, osPost;
    TriodeStage triode;
    PowerAmpStage powerAmp;
    HarmonicEngine harmonics;
    ToneResidual residual;
    ToneMatch toneMatch;
    DualCabinet cabinet;

    Smoothed inGain, residualIn, residualOut, volumeGain, masterGain, outGain, tubeMix, tubeDrive, paDrive;
    double dipEnv = 1.0;
    int dipPhase = 0, dipCounter = 0;
    std::atomic<bool> dipRequested { false };

    std::vector<double> mono, ampOut, ampTmp, scratch;
    juce::AudioBuffer<float> stereo;
    std::vector<float> monoF;
    std::atomic<int> latency { 0 };
    int runtimeLatency = 0;
};
} // namespace namvx
