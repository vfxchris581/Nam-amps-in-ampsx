#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namvx
{
/** The amplifier: faceplate generated from the loaded amp (name, channels, eight controls). */
class AmpPanel : public juce::Component, private juce::Timer
{
public:
    explicit AmpPanel (NamVXProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Rectangle<int> plateBounds() const;

    NamVXProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::OwnedArray<juce::TextButton> channelButtons;
    Knob input, output;
    Toggle tube, power, harm, cab;
    Meter inMeter, outMeter;
    juce::Label blendInfo;
    juce::String ampName, manufacturer;
    int lastChannelCount = -1;
};
} // namespace namvx
