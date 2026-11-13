#pragma once
#include "AmpPanel.h"
#include "CapturesPanel.h"
#include "CabinetPanel.h"
#include "TubePanel.h"
#include "LibraryPanel.h"
#include "ToneMatrixPanel.h"
#include "EqPanel.h"

namespace namknobs
{
class NamKnobsEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener
{
public:
    explicit NamKnobsEditor (NamKnobsProcessor&);
    ~NamKnobsEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    enum Tab { tabAmp = 0, tabToneMatrix, tabEq, tabCaptures, tabCabinet, tabTube, tabLibrary };
    void showTab (int index) { tabs.setCurrentTabIndex (index); }
    static constexpr int kWidth = 1180, kHeight = 740;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshAll();

    NamKnobsProcessor& proc;
    NamKnobsLookAndFeel lnf;
    juce::Component content;
    juce::ComboBox ampSelector, presetSelector;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "Save" };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    AmpPanel ampPanel;
    CapturesPanel capturesPanel;
    CabinetPanel cabinetPanel;
    TubePanel tubePanel;
    LibraryPanel libraryPanel;
    ToneMatrixPanel toneMatrixPanel;
    EqPanel eqPanel;
    bool dragging = false;
};
} // namespace namknobs
