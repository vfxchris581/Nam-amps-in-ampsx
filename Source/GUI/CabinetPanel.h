#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namvx
{
/** Professional dual IR loader. */
class CabinetPanel : public juce::Component
{
public:
    explicit CabinetPanel (NamVXProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Slot
    {
        Slot (NamVXProcessor& p, int index);
        std::unique_ptr<Toggle> on, invert;
        std::unique_ptr<Knob> level, pan, delay;
        juce::TextButton load { "Load IR..." }, clear { "Clear" };
        juce::Label file, info;
    };

    NamVXProcessor& proc;
    Slot s1, s2;
    Knob blend, level;
    Toggle stereo, autoAlign;
    juce::Label alignInfo;
    juce::ComboBox cabPresets;
    juce::TextButton saveCab { "Save cab preset" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> slotArea[2], globalArea;
};
} // namespace namvx
