#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <mutex>
#include "AudioEngine/SignalChain.h"
#include "PresetManager/AmpLibrary.h"
#include "PresetManager/PresetManager.h"

namespace namified
{
class NamifiedProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater,
                          private juce::Timer
{
public:
    NamifiedProcessor();
    /** For tools/tests: use a specific library folder. */
    explicit NamifiedProcessor (const juce::File& libraryRoot);
    ~NamifiedProcessor() override;

    // ---- AudioProcessor ------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Namified"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Namified API (message thread) -----------------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    AmpLibrary& getLibrary() noexcept { return *library; }
    PresetManager& getPresets() noexcept { return *presets; }
    SignalChain& getChain() noexcept { return chain; }

    const AmpDefinition* getCurrentAmp() const;
    juce::String getCurrentAmpId() const { return currentAmpId; }
    void loadAmp (const juce::String& ampId);
    /** Save an edited definition (mappings, channels, names) and rebuild its runtime. */
    void updateAmp (const AmpDefinition& def, bool rebuild = true);
    void deleteAmp (const juce::String& ampId);
    int getCurrentChannel() const;

    /** Background import with analysis; progress/finish reported through the ChangeBroadcaster. */
    void importFiles (const juce::StringArray& paths);
    bool isImporting() const noexcept { return importing.load(); }
    juce::String getStatus() const;
    std::vector<PendingCapture> getPending() const;
    void assignPending (int index, const juce::String& make, const juce::String& model, const juce::String& channel);
    void discardPending (int index);
    /** Re-run the sound analysis of every capture in the current amp (background). */
    void reanalyseCurrentAmp();

    juce::String loadImpulseResponse (int slot, const juce::File& f);
    void clearImpulseResponse (int slot);
    juce::String getIRPath (int slot) const { return irPaths[slot]; }

    struct EngineOptions
    {
        int oversampling = 2;     // 1 = 2x, 2 = 4x, 3 = 8x
        bool linearPhase = false;
        int maxActive = 3;
        double loudnessComp = 1.0;
        bool alwaysOn = false;
    };
    EngineOptions getEngineOptions() const { return options; }
    void setEngineOptions (const EngineOptions& o);

    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File& f);
    juce::String getCurrentPresetName() const { return currentPresetName; }

private:
    void init (const juce::File& libraryRoot);
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void rebuildRuntime();
    void snapshotParameters (EngineParameters& p) const noexcept;
    template <typename T> void processAny (juce::AudioBuffer<T>& buffer);
    void setStatus (const juce::String& s);

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<AmpLibrary> library;
    std::unique_ptr<PresetManager> presets;
    SignalChain chain;
    juce::ThreadPool workers { 1 };

    juce::String currentAmpId, currentPresetName;
    juce::String irPaths[2];
    EngineOptions options;
    std::atomic<bool> importing { false };
    std::atomic<int> buildGeneration { 0 };
    std::atomic<int> pendingChannel { -1 };
    int lastChannel = 0;
    bool restoringChannelKnobs = false;

    mutable std::mutex statusLock;
    juce::String status;
    std::vector<PendingCapture> pending;

    std::array<std::atomic<float>*, kNumControls> knobParams {};
    std::atomic<float>* raw (const char* id) const { return apvts.getRawParameterValue (id); }
    struct Raw
    {
        std::atomic<float>*channel, *input, *output, *preTubeOn, *preTubeDrive, *preTubeMix, *powerAmpOn, *tubeType,
            *bias, *sag, *nfb, *transformer, *mismatch, *speaker, *harmOn, *harmAmount, *harmEvenOdd, *harmTouch,
            *harmAttack, *harmLowDef, *harmHighDetail, *cabOn, *cabStereo, *cabAutoAlign, *cabBlend, *cabLevel,
            *ir1On, *ir1Level, *ir1Pan, *ir1Invert, *ir1Delay, *ir2On, *ir2Level, *ir2Pan, *ir2Invert, *ir2Delay;
    } rp {};
    double preparedRate = 0.0;
    int preparedBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamifiedProcessor)
};
} // namespace namified
