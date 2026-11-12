#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namified
{
/** Amp library (load / rename / delete amps, manage channels) and the preset browser. */
class LibraryPanel : public juce::Component
{
public:
    explicit LibraryPanel (NamifiedProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct AmpList : juce::ListBoxModel
    {
        LibraryPanel* owner = nullptr;
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void selectedRowsChanged (int) override;
    };
    struct PresetList : juce::ListBoxModel
    {
        LibraryPanel* owner = nullptr;
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    };
    void askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    const AmpDefinition* selectedAmp() const;

    NamifiedProcessor& proc;
    AmpList ampModel;
    PresetList presetModel;
    juce::ListBox amps { "amps", &ampModel }, presets { "presets", &presetModel };
    juce::TextButton loadAmp { "Load" }, renameAmp { "Rename" }, deleteAmp { "Delete" };
    juce::TextButton renameChannel { "Rename channel" }, addChannel { "Add channel" }, removeChannel { "Remove channel" };
    juce::ComboBox channelBox;
    juce::Label ampDetails;
    juce::TextButton loadPreset { "Load" }, savePreset { "Save current..." }, renamePreset { "Rename" }, deletePreset { "Delete" };
    juce::Array<juce::File> presetFiles;
    juce::Rectangle<int> ampArea, presetArea;
};
} // namespace namified
