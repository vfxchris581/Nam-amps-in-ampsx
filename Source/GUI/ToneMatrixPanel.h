#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namvx
{
/** Eight NAM loaders that play as one amp.
    Top: the eight loader slots. Middle: a real amp faceplate (Gain, Volume, Bass, Middle, Treble,
    Presence, Resonance, Master) - in Auto mode turning these blends the loaded captures the way a
    real amp's knobs would. Bottom: the blend matrix (rows = controls, columns = slots) showing what
    the knobs are doing; with Auto off you drag it by hand. */
class ToneMatrixPanel : public juce::Component, private juce::Timer
{
public:
    explicit ToneMatrixPanel (NamVXProcessor& p);
    ~ToneMatrixPanel() override;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kRows = 6; // Gain, Resonance, Bass, Middle, Treble, Presence
    static constexpr int kSlots = kBlendSlots;

private:
    struct CellMouse;
    void timerCallback() override;
    void setRow (int row, int soloSlot);
    void showCellMenu (int row, int slot);
    void applyAutoState();
    juce::String paramId (int row, int slot) const;

    NamVXProcessor& proc;
    std::vector<std::unique_ptr<Knob>> ampKnobs;
    juce::Slider cells[kRows][kSlots];
    std::unique_ptr<APVTS::SliderAttachment> attach[kRows][kSlots];
    std::unique_ptr<CellMouse> mouse[kRows][kSlots];
    juce::TextButton load[kSlots], clear[kSlots];
    juce::Label names[kSlots], readout[kRows];
    Toggle autoToggle;
    juce::TextButton resetAll { "Even blend" };
    std::unique_ptr<juce::FileChooser> chooser;

    bool loaded[kSlots] {};
    int numLoaded = 0;
    bool autoMode = true, lastAuto = true;
    juce::Rectangle<int> slotsArea, plate, matrixArea, grid;
    int labelW = 120, readW = 66;
};
} // namespace namvx
