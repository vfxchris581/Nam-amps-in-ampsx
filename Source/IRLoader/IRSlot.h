#pragma once
// One cabinet impulse-response slot: WAV/AIFF loading (mono or stereo, any rate - resampled to
// the session rate by the convolution engine), energy normalisation, onset detection, plus the
// slot's level / pan / polarity / delay / alignment processing.

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace namvx
{
struct IRInfo
{
    juce::File file;
    double sampleRate = 0.0;
    int numChannels = 0;
    int lengthSamples = 0;
    double onsetMs = 0.0;
    bool loaded() const noexcept { return lengthSamples > 0; }
};

class IRSlot
{
public:
    IRSlot();

    /** Message thread. Returns an error string (empty on success). */
    juce::String loadFile (const juce::File& f);
    /** Message thread: load already-decoded IR data (used by tests and presets). */
    juce::String loadBuffer (juce::AudioBuffer<float> ir, double irSampleRate, const juce::File& sourceName = {});
    void clear();

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    struct Params
    {
        bool enabled = true;
        float levelDb = 0.0f;
        float pan = 0.0f;          // -1..1
        bool invert = false;
        float delayMs = 0.0f;      // manual 0..10 ms
        float alignMs = 0.0f;      // automatic alignment offset (>= 0)
    };
    void setParams (const Params& p) noexcept { params = p; }

    /** Audio thread: convolve the mono input into 'out' (2 channels), gain/pan/polarity/delay applied. */
    void process (const float* monoIn, juce::AudioBuffer<float>& out, int n) noexcept;

    const IRInfo& getInfo() const noexcept { return info; }
    /** Copy of the normalised IR (mono sum) used for alignment analysis. */
    const juce::AudioBuffer<float>& getAnalysisCopy() const noexcept { return analysisCopy; }
    bool isLoaded() const noexcept { return loadedFlag.load(); }

private:
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 256 } };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delay { 48000 };
    juce::AudioBuffer<float> work;
    juce::AudioBuffer<float> analysisCopy;
    IRInfo info;
    Params params;
    juce::SmoothedValue<float> gainL, gainR, delaySmoothed;
    double sampleRate = 48000.0;
    std::atomic<bool> loadedFlag { false };
    std::atomic<bool> stereoIR { false };
};
} // namespace namvx
