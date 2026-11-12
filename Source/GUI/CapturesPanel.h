#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namvx
{
/** NAM profile manager: every capture of the current amp, its channel and its knob mapping
    (detected from names, estimated from sound, or set by you), plus manual assignment of
    captures that had no manufacturer/model metadata. */
class CapturesPanel : public juce::Component, private juce::TableListBoxModel
{
public:
    explicit CapturesPanel (NamVXProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row { int channel; int index; };
    int getNumRows() override { return (int) rows.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int col, bool selected, juce::Component* existing) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    juce::String cellText (int row, int col) const;
    void cellEdited (int row, int col, const juce::String& text);
    void markDirty();
    void showPending();

    class EditableCell;

    NamVXProcessor& proc;
    AmpDefinition working;
    bool dirty = false;
    std::vector<Row> rows;
    juce::TableListBox table { "captures", this };
    juce::TextButton importButton { "Import .nam files..." }, applyButton { "Apply mapping" },
        analyseButton { "Re-analyse sound" }, revertButton { "Revert" };
    std::unique_ptr<juce::FileChooser> chooser;

    // pending (unidentified) captures
    juce::Label pendingTitle;
    juce::TextEditor makeEd, modelEd, channelEd;
    juce::TextButton assignButton { "Create / add to amp" }, skipButton { "Discard" };
    int pendingIndex = 0;

    enum Cols { colName = 1, colChannel, colFirstKnob, colTrim = colFirstKnob + kNumControls, colOn, colInfo };
};
} // namespace namvx
