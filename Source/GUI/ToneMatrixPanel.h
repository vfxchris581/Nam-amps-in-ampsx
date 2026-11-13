#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namknobs
{
/** Eight NAM loaders that play as one amp.
    Top: the eight loader slots. Middle: a real amp faceplate (Gain, Volume, Bass, Middle, Treble,
    Presence, Resonance, Master) - in Auto mode turning these blends the loaded captures the way a
    real amp's knobs would. Bottom: the blend matrix (rows = controls, columns = slots) showing what
    the knobs are doing; with Auto off you drag it by hand. */
class ToneMatrixPanel : public juce::Component, private juce::Timer
{
public:
    explicit ToneMatrixPanel (NamKnobsProcessor& p);
    ~ToneMatrixPanel() override;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kRows = 6; // Gain, Resonance, Bass, Middle, Treble, Presence
    static constexpr int kSlots = kBlendSlots;

private:
    struct CellMouse;
    struct MatrixCell;
    void timerCallback() override;
    void setRow (int row, int soloSlot);
    void showCellMenu (int row, int slot);
    void applyAutoState();
    juce::String paramId (int row, int slot) const;

    NamKnobsProcessor& proc;
    std::vector<std::unique_ptr<Knob>> ampKnobs;
    std::unique_ptr<MatrixCell> cells[kRows][kSlots];
    std::unique_ptr<APVTS::SliderAttachment> attach[kRows][kSlots];
    std::unique_ptr<CellMouse> mouse[kRows][kSlots];
    juce::TextButton load[kSlots], clear[kSlots];
    std::unique_ptr<Toggle> invert[kSlots];
    Choice phase;
    juce::Label phaseLabel { {}, "Phase" };
    juce::Label names[kSlots], matchLabel[kSlots], readout[kRows];
    Toggle levelMatch;
    juce::Label statusLabel;
    juce::Slider trim[kRows];                                  // fine-tune per tone row (rows 1..5)
    std::unique_ptr<APVTS::SliderAttachment> trimAttach[kRows];
    Toggle autoToggle;
    juce::TextButton resetAll { "Even blend" };
    juce::TextButton deleteAll { "Delete all" };
    std::unique_ptr<juce::FileChooser> chooser;

    bool loaded[kSlots] {};
    int numLoaded = 0;
    bool autoMode = true, lastAuto = true, building = false;
    juce::Rectangle<int> slotsArea, plate, matrixArea, grid;
    int labelW = 150, readW = 58, nameW = 104;
};
} // namespace namknobs
