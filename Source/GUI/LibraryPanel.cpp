#include "LibraryPanel.h"
#include "ChannelManager/ChannelManager.h"

namespace namknobs
{
int LibraryPanel::AmpList::getNumRows() { return (int) owner->proc.getLibrary().getAmps().size(); }

void LibraryPanel::AmpList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& all = owner->proc.getLibrary().getAmps();
    if (row >= (int) all.size()) return;
    const auto& a = all[(size_t) row];
    const bool current = a.id == owner->proc.getCurrentAmpId().toStdString();
    g.setColour (selected ? ui::gold.withAlpha (0.18f) : juce::Colours::transparentBlack);
    g.fillRoundedRectangle (2, 1, (float) w - 4, (float) h - 2, 5);
    if (current) ui::drawLed (g, { 12, h * 0.5f - 4, 8, 8 }, ui::amber, true);
    g.setColour (ui::text);
    g.setFont (ui::semi (14.0f));
    g.drawText (a.name, 30, 4, w - 40, h / 2, juce::Justification::centredLeft);
    g.setColour (ui::textDim);
    g.setFont (ui::body (12.0f));
    g.drawText (a.isBlend ? juce::String::fromUTF8 ("Model blend  \xc2\xb7  ") + juce::String (a.totalCaptures()) + " models"
                          : juce::String (a.manufacturer) + "  \xc2\xb7  " + juce::String ((int) a.channels.size()) + " ch  \xc2\xb7  "
                                + juce::String (a.totalCaptures()) + " captures",
                30, h / 2, w - 40, h / 2 - 4, juce::Justification::centredLeft);
}

void LibraryPanel::AmpList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    const auto& all = owner->proc.getLibrary().getAmps();
    if (row < (int) all.size()) owner->proc.loadAmp (all[(size_t) row].id);
}

void LibraryPanel::AmpList::selectedRowsChanged (int) { owner->refresh(); }

int LibraryPanel::PresetList::getNumRows() { return owner->presetFiles.size(); }

void LibraryPanel::PresetList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row >= owner->presetFiles.size()) return;
    g.setColour (selected ? ui::gold.withAlpha (0.18f) : juce::Colours::transparentBlack);
    g.fillRoundedRectangle (2, 1, (float) w - 4, (float) h - 2, 5);
    const auto name = owner->presetFiles[row].getFileNameWithoutExtension();
    g.setColour (name == owner->proc.getCurrentPresetName() ? ui::amber : ui::text);
    g.setFont (ui::body (14.0f));
    g.drawText (name, 14, 0, w - 20, h, juce::Justification::centredLeft);
}

void LibraryPanel::PresetList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row < owner->presetFiles.size()) owner->proc.loadPreset (owner->presetFiles[row]);
}

LibraryPanel::LibraryPanel (NamKnobsProcessor& p) : proc (p)
{
    ampModel.owner = this;
    presetModel.owner = this;
    amps.setRowHeight (48);
    presets.setRowHeight (30);
    for (juce::Component* c : { (juce::Component*) &amps, (juce::Component*) &presets, (juce::Component*) &loadAmp,
                                (juce::Component*) &renameAmp, (juce::Component*) &deleteAmp, (juce::Component*) &renameChannel,
                                (juce::Component*) &addChannel, (juce::Component*) &removeChannel, (juce::Component*) &channelBox,
                                (juce::Component*) &ampDetails, (juce::Component*) &loadPreset, (juce::Component*) &savePreset,
                                (juce::Component*) &renamePreset, (juce::Component*) &deletePreset,
                                (juce::Component*) &newBlend, (juce::Component*) &blendHelp })
        addAndMakeVisible (c);
    ampDetails.setFont (ui::body (12.5f));
    ampDetails.setColour (juce::Label::textColourId, ui::textDim);
    ampDetails.setJustificationType (juce::Justification::topLeft);

    newBlend.setColour (juce::TextButton::buttonColourId, ui::gold.withAlpha (0.22f));
    blendHelp.setFont (ui::body (12.0f));
    blendHelp.setColour (juce::Label::textColourId, ui::textDim);
    blendHelp.setJustificationType (juce::Justification::topLeft);
    blendHelp.setText ("Mix 2-4 NAM models of different amps into one amp. Levels and timing are matched "
                       "automatically; set the mix with the Model knobs on the Amp page.", juce::dontSendNotification);
    newBlend.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Pick 2-4 NAM models to blend", juce::File(), "*.nam;*.NAM");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) {
                                  juce::StringArray paths;
                                  for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                  if (paths.isEmpty()) return;
                                  if (paths.size() < 2 || paths.size() > AmpDefinition::kMaxBlendModels)
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Model blend",
                                                                              "Select between 2 and 4 .nam files.");
                                      return;
                                  }
                                  askText ("Name this blend", "My Blend", [this, paths] (juce::String n) { proc.createBlend (n, paths); });
                              });
    };

    loadAmp.onClick = [this] { if (auto* a = selectedAmp()) proc.loadAmp (a->id); };
    renameAmp.onClick = [this] {
        if (auto* a = selectedAmp())
            askText ("Rename amplifier", a->name, [this, id = a->id] (juce::String n) { proc.getLibrary().rename (id, n.toStdString()); refresh(); proc.sendChangeMessage(); });
    };
    deleteAmp.onClick = [this] {
        if (auto* a = selectedAmp())
        {
            const auto id = juce::String (a->id);
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete amplifier",
                                                "Delete \"" + juce::String (a->name) + "\" and its library copies of the captures?",
                                                "Delete", "Cancel", this,
                                                juce::ModalCallbackFunction::create ([this, id] (int r) { if (r) { proc.deleteAmp (id); refresh(); } }));
        }
    };
    deleteAllAmps.setColour (juce::TextButton::textColourOffId, ui::red.brighter (0.4f));
    deleteAllAmps.setTooltip ("Empty the whole amp library (amps, blends and their copies of the NAM captures)");
    deleteAllAmps.onClick = [this] {
        const int n = (int) proc.getLibrary().getAmps().size();
        if (n == 0) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete all amps & captures",
            "Delete all " + juce::String (n) + (n == 1 ? " amp" : " amps") + " and blends, with the library's copies of their NAM captures?"
            "\n\nYour original .nam files and your presets are not touched.",
            "Delete all", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r) { proc.deleteAllAmps(); refresh(); } }));
    };
    addAndMakeVisible (deleteAllAmps);
    renameChannel.onClick = [this] {
        auto* a = selectedAmp();
        const int c = channelBox.getSelectedItemIndex();
        if (a == nullptr || c < 0) return;
        askText ("Rename channel", a->channels[(size_t) c].name, [this, def = *a, c] (juce::String n) mutable {
            if (ChannelManager::renameChannel (def, c, n.toStdString())) proc.updateAmp (def, false);
            refresh();
        });
    };
    addChannel.onClick = [this] {
        auto* a = selectedAmp();
        if (a == nullptr) return;
        askText ("Add channel", "Channel " + juce::String ((int) a->channels.size() + 1), [this, def = *a] (juce::String n) mutable {
            ChannelManager::addChannel (def, n.toStdString());
            proc.updateAmp (def);
            refresh();
        });
    };
    removeChannel.onClick = [this] {
        auto* a = selectedAmp();
        const int c = channelBox.getSelectedItemIndex();
        if (a == nullptr || c < 0) return;
        AmpDefinition def = *a;
        if (ChannelManager::removeChannel (def, c)) proc.updateAmp (def);
        refresh();
    };

    loadPreset.onClick = [this] { const int r = presets.getSelectedRow(); if (r >= 0 && r < presetFiles.size()) proc.loadPreset (presetFiles[r]); };
    savePreset.onClick = [this] {
        askText ("Save preset", proc.getCurrentPresetName().isEmpty() ? juce::String ("My Tone") : proc.getCurrentPresetName(),
                 [this] (juce::String n) { proc.savePreset (n); refresh(); });
    };
    renamePreset.onClick = [this] {
        const int r = presets.getSelectedRow();
        if (r < 0 || r >= presetFiles.size()) return;
        askText ("Rename preset", presetFiles[r].getFileNameWithoutExtension(),
                 [this, f = presetFiles[r]] (juce::String n) { proc.getPresets().renamePreset (f, n); refresh(); });
    };
    deletePreset.onClick = [this] {
        const int r = presets.getSelectedRow();
        if (r >= 0 && r < presetFiles.size()) { proc.getPresets().deletePreset (presetFiles[r]); refresh(); }
    };
    refresh();
}

const AmpDefinition* LibraryPanel::selectedAmp() const
{
    const auto& all = proc.getLibrary().getAmps();
    const int r = amps.getSelectedRow();
    if (r >= 0 && r < (int) all.size()) return &all[(size_t) r];
    return proc.getCurrentAmp();
}

void LibraryPanel::askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("t", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r) {
        if (r == 1 && w->getTextEditorContents ("t").trim().isNotEmpty()) done (w->getTextEditorContents ("t").trim());
    }), true);
}

void LibraryPanel::refresh()
{
    amps.updateContent();
    amps.repaint();
    presetFiles = proc.getPresets().listPresets();
    presets.updateContent();
    presets.repaint();

    channelBox.clear (juce::dontSendNotification);
    if (auto* a = selectedAmp())
    {
        for (size_t i = 0; i < a->channels.size(); ++i)
            channelBox.addItem (juce::String (a->channels[i].name) + "  (" + juce::String ((int) a->channels[i].captures.size()) + ")", (int) i + 1);
        channelBox.setSelectedItemIndex (0, juce::dontSendNotification);
        juce::String d;
        d << "Manufacturer: " << juce::String (a->manufacturer) << "\nModel: " << juce::String (a->model)
          << "\nChannels: " << (int) a->channels.size() << "\nCaptures: " << a->totalCaptures();
        ampDetails.setText (d, juce::dontSendNotification);
    }
    else
        ampDetails.setText ("Your amp library is empty. Drop .nam files of one amplifier anywhere on the window.", juce::dontSendNotification);
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    ui::drawPanel (g, ampArea.toFloat(), "Amp library");
    ui::drawPanel (g, presetArea.toFloat(), "Presets");
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (24, 18);
    presetArea = r.removeFromRight (r.getWidth() * 2 / 5);
    r.removeFromRight (16);
    ampArea = r;

    auto a = ampArea.reduced (16, 12);
    a.removeFromTop (24);
    auto side = a.removeFromRight (230);
    a.removeFromRight (12);
    amps.setBounds (a);
    auto row = side.removeFromTop (30);
    const int bw = (row.getWidth() - 12) / 3;
    loadAmp.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (6);
    renameAmp.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (6);
    deleteAmp.setBounds (row);
    side.removeFromTop (12);
    ampDetails.setBounds (side.removeFromTop (80));
    side.removeFromTop (8);
    channelBox.setBounds (side.removeFromTop (30));
    side.removeFromTop (8);
    for (auto* b : { &renameChannel, &addChannel, &removeChannel }) { b->setBounds (side.removeFromTop (30)); side.removeFromTop (6); }
    side.removeFromTop (18);
    newBlend.setBounds (side.removeFromTop (34));
    side.removeFromTop (6);
    blendHelp.setBounds (side.removeFromTop (64));
    deleteAllAmps.setBounds (side.removeFromBottom (30));

    auto p = presetArea.reduced (16, 12);
    p.removeFromTop (24);
    auto buttons = p.removeFromBottom (30);
    const int pw = (buttons.getWidth() - 18) / 4;
    for (auto* b : { &loadPreset, &savePreset, &renamePreset, &deletePreset }) { b->setBounds (buttons.removeFromLeft (pw)); buttons.removeFromLeft (6); }
    p.removeFromBottom (10);
    presets.setBounds (p);
}
} // namespace namknobs
