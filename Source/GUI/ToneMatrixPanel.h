#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namvx
{
/** Blend amps as one whole amp: for Gain and each EQ band, choose (and mix) which NAM captures
    it comes from. Rows = controls, columns = the blend's models. */
class ToneMatrixPanel : public juce::Component, private juce::Timer
{
public:
    explicit ToneMatrixPanel (NamVXProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setRow (int row, int soloModel); // soloModel < 0 = even mix
    juce::String paramId (int row, int model) const;

    static constexpr int kRows = 6;      // Gain, Resonance, Bass, Middle, Treble, Presence
    static constexpr int kModels = 4;

    NamVXProcessor& proc;
    juce::Slider cells[kRows][kModels];
    std::unique_ptr<APVTS::SliderAttachment> attach[kRows][kModels];
    juce::TextButton solo[kRows][kModels];
    juce::TextButton even[kRows];
    juce::Label modelNames[kModels];
    juce::Label readout[kRows];
    juce::TextButton resetAll { "Reset to even blend" };
    int numModels = 0;
    bool isBlend = false;
    juce::Rectangle<int> area, grid;
};
} // namespace namvx
