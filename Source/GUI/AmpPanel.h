#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namknobs
{
/** The amplifier: faceplate generated from the loaded amp (name, channels, eight controls). */
class AmpPanel : public juce::Component, private juce::Timer
{
public:
    explicit AmpPanel (NamKnobsProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Rectangle<int> plateBounds() const;

    NamKnobsProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::OwnedArray<juce::TextButton> channelButtons;
    Knob input, output, gateThreshold, gateRelease;
    Toggle gateOn;
    juce::Rectangle<int> gateArea;
    float gateLed = 0.0f;
    std::vector<std::unique_ptr<Knob>> mixKnobs;
    int numMix = 0;
    Toggle tube, power, harm, cab;
    Meter inMeter, outMeter;
    juce::Label blendInfo;
    juce::String ampName, manufacturer;
    int lastChannelCount = -1;
};
} // namespace namknobs
