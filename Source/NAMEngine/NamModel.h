#pragma once
// Real-time wrapper around the official NeuralAmpModelerCore inference engine.
// - loads .nam files via nam::get_dsp
// - runs the network at its training sample rate (sinc resampling in/out when the host differs)
// - fixed, reported latency; no allocation in process()

#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include "SincResampler.h"

namespace nam { class DSP; }

namespace namvx
{
class NamModel
{
public:
    NamModel();
    ~NamModel();
    NamModel (const NamModel&) = delete;
    NamModel& operator= (const NamModel&) = delete;

    /** Loads a .nam file. Throws std::runtime_error with a readable message on failure. */
    void load (const std::filesystem::path& file);

    /** Must be called (off the audio thread) before process(). */
    void prepare (double hostSampleRate, int maxBlockSize);

    /** Mono in/out, in-place allowed. n <= maxBlockSize. */
    void process (const double* in, double* out, int n) noexcept;

    /** Latency added by resampling (host samples). 0 when the host runs at the model's rate. */
    int getResamplingLatency() const noexcept { return resampleLatency; }
    double getModelSampleRate() const noexcept { return modelRate; }
    bool isResampling() const noexcept { return needsResampling; }

    bool hasLoudness() const noexcept;
    double getLoudness() const noexcept;      // dB, from NAM metadata
    bool hasInputLevel() const noexcept;
    double getInputLevelDbu() const noexcept;
    const std::string& getArchitecture() const noexcept { return architecture; }
    int getReceptiveField() const noexcept { return prewarmSamples; }

private:
    std::unique_ptr<nam::DSP> dsp;
    std::string architecture;
    double modelRate = 48000.0, hostRate = 48000.0;
    bool needsResampling = false;
    int maxBlock = 0, modelMaxBlock = 0, resampleLatency = 0, prewarmSamples = 0;

    SincResampler up, down;
    SampleFifo modelIn, hostOut;
    std::vector<double> scratchIn, scratchOut;
};
} // namespace namvx
