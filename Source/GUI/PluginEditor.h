#pragma once
#include "AmpPanel.h"
#include "CapturesPanel.h"
#include "CabinetPanel.h"
#include "TubePanel.h"
#include "LibraryPanel.h"

namespace namified
{
class NamifiedEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener
{
public:
    explicit NamifiedEditor (NamifiedProcessor&);
    ~NamifiedEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    void showTab (int index) { tabs.setCurrentTabIndex (index); }
    static constexpr int kWidth = 1180, kHeight = 740;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshAll();

    NamifiedProcessor& proc;
    NamifiedLookAndFeel lnf;
    juce::Component content;
    juce::ComboBox ampSelector, presetSelector;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "Save" };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    AmpPanel ampPanel;
    CapturesPanel capturesPanel;
    CabinetPanel cabinetPanel;
    TubePanel tubePanel;
    LibraryPanel libraryPanel;
    bool dragging = false;
};
} // namespace namified
