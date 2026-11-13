#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namknobs
{
/** 12AX7 stage, power amp circuit, harmonic engine and engine quality settings. */
class TubePanel : public juce::Component
{
public:
    explicit TubePanel (NamKnobsProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void pushOptions();

    NamKnobsProcessor& proc;
    Toggle preOn, paOn, harmOn;
    Knob preDrive, preMix;
    Choice tubeType;
    Knob bias, sag, nfb, xfmr, mismatch, speaker;
    Knob amount, evenOdd, touch, attack, lowDef, highDetail;
    juce::ComboBox osBox, maxActiveBox;
    juce::ToggleButton linearPhase { "Linear-phase oversampling" }, alwaysOn { "All captures always running (more CPU)" };
    juce::Slider loudness;
    juce::Label loudnessLabel { {}, "Capture loudness compensation" }, osLabel { {}, "Oversampling" }, maxLabel { {}, "Max blended captures" };
    juce::Rectangle<int> areas[4];
};
} // namespace namknobs
