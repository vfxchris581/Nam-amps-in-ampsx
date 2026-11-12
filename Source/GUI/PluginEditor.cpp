#include "PluginEditor.h"

namespace namvx
{
NamVXEditor::NamVXEditor (NamVXProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      ampPanel (p), capturesPanel (p), cabinetPanel (p), tubePanel (p), libraryPanel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    for (juce::Component* c : { (juce::Component*) &ampSelector, (juce::Component*) &presetSelector, (juce::Component*) &prevPreset,
                                (juce::Component*) &nextPreset, (juce::Component*) &savePreset, (juce::Component*) &tabs })
        content.addAndMakeVisible (c);

    tabs.setTabBarDepth (40);
    tabs.setOutline (0);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, ui::bg);
    tabs.addTab ("Amp", ui::bg, &ampPanel, false);
    tabs.addTab ("Captures", ui::bg, &capturesPanel, false);
    tabs.addTab ("Cabinet", ui::bg, &cabinetPanel, false);
    tabs.addTab ("Tube & Harmonics", ui::bg, &tubePanel, false);
    tabs.addTab ("Library", ui::bg, &libraryPanel, false);

    ampSelector.setTextWhenNothingSelected ("No amp - drop .nam files");
    ampSelector.onChange = [this] {
        const int i = ampSelector.getSelectedItemIndex();
        const auto& amps = proc.getLibrary().getAmps();
        if (i >= 0 && i < (int) amps.size() && amps[(size_t) i].id != proc.getCurrentAmpId().toStdString())
            proc.loadAmp (amps[(size_t) i].id);
    };
    presetSelector.setTextWhenNothingSelected ("Presets");
    presetSelector.onChange = [this] {
        const int i = presetSelector.getSelectedItemIndex();
        const auto files = proc.getPresets().listPresets();
        if (i >= 0 && i < files.size() && files[i].getFileNameWithoutExtension() != proc.getCurrentPresetName())
            proc.loadPreset (files[i]);
    };
    auto step = [this] (int d) {
        const auto files = proc.getPresets().listPresets();
        if (files.isEmpty()) return;
        int i = presetSelector.getSelectedItemIndex();
        i = (i + d + files.size()) % files.size();
        proc.loadPreset (files[i]);
    };
    prevPreset.onClick = [step] { step (-1); };
    nextPreset.onClick = [step] { step (1); };
    savePreset.onClick = [this] {
        auto* w = new juce::AlertWindow ("Save preset", {}, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("t", proc.getCurrentPresetName().isEmpty() ? juce::String ("My Tone") : proc.getCurrentPresetName());
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
            if (r == 1) proc.savePreset (w->getTextEditorContents ("t").trim());
        }), true);
    };

    proc.addChangeListener (this);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / kHeight);
    setResizeLimits (kWidth * 6 / 10, kHeight * 6 / 10, kWidth * 2, kHeight * 2);
    setSize (kWidth, kHeight);
    refreshAll();
}

NamVXEditor::~NamVXEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void NamVXEditor::changeListenerCallback (juce::ChangeBroadcaster*) { refreshAll(); }

void NamVXEditor::refreshAll()
{
    ampSelector.clear (juce::dontSendNotification);
    int id = 1, sel = 0;
    for (auto& a : proc.getLibrary().getAmps())
    {
        ampSelector.addItem (a.name, id);
        if (a.id == proc.getCurrentAmpId().toStdString()) sel = id;
        ++id;
    }
    ampSelector.setSelectedId (sel, juce::dontSendNotification);

    presetSelector.clear (juce::dontSendNotification);
    id = 1; sel = 0;
    for (auto& f : proc.getPresets().listPresets())
    {
        presetSelector.addItem (f.getFileNameWithoutExtension(), id);
        if (f.getFileNameWithoutExtension() == proc.getCurrentPresetName()) sel = id;
        ++id;
    }
    presetSelector.setSelectedId (sel, juce::dontSendNotification);

    ampPanel.refresh();
    capturesPanel.refresh();
    cabinetPanel.refresh();
    tubePanel.refresh();
    libraryPanel.refresh();
}

void NamVXEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
}

void NamVXEditor::paintOverChildren (juce::Graphics& g)
{
    // header (drawn in the scaled content's coordinate space)
    const float s = (float) getWidth() / kWidth;
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale (s));
    auto logo = juce::Rectangle<float> (24, 14, 260, 40);
    g.setColour (ui::gold);
    const auto logoFont = ui::brand (28.0f).withExtraKerningFactor (0.04f);
    g.setFont (logoFont);
    g.drawText ("Nam VX", logo, juce::Justification::centredLeft);
    const float logoW = juce::GlyphArrangement::getStringWidth (logoFont, "Nam VX");
    g.setColour (ui::textDim);
    g.setFont (ui::semi (9.5f).withExtraKerningFactor (0.3f));
    g.drawText ("INTELLIGENT NAM AMPLIFIER", logo.translated (logoW + 18.0f, 2), juce::Justification::centredLeft);

    if (dragging)
    {
        auto r = juce::Rectangle<float> (0, 0, (float) kWidth, (float) kHeight).reduced (10);
        g.setColour (ui::bg.withAlpha (0.82f));
        g.fillRoundedRectangle (r, 12);
        g.setColour (ui::gold);
        g.drawRoundedRectangle (r, 12, 2.0f);
        g.setFont (ui::brand (26.0f));
        g.drawText ("Drop NAM captures of one amp  \xc2\xb7  or WAV impulse responses", r, juce::Justification::centred);
    }
}

void NamVXEditor::resized()
{
    content.setBounds (0, 0, kWidth, kHeight);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / kWidth));

    auto r = juce::Rectangle<int> (0, 0, kWidth, kHeight);
    auto header = r.removeFromTop (66).reduced (24, 16);
    savePreset.setBounds (header.removeFromRight (64));
    header.removeFromRight (6);
    nextPreset.setBounds (header.removeFromRight (34));
    header.removeFromRight (4);
    presetSelector.setBounds (header.removeFromRight (220));
    header.removeFromRight (4);
    prevPreset.setBounds (header.removeFromRight (34));
    header.removeFromRight (18);
    ampSelector.setBounds (header.removeFromRight (260));
    tabs.setBounds (r);
}

bool NamVXEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory() || file.hasFileExtension ("nam;wav;aif;aiff")) return true;
    }
    return false;
}

void NamVXEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    juce::StringArray nams;
    int irSlot = proc.getChain().getCabinet().slot (0).isLoaded() ? 1 : 0;
    for (auto& f : files)
    {
        const juce::File file (f);
        if (file.hasFileExtension ("wav;aif;aiff"))
        {
            proc.loadImpulseResponse (irSlot, file);
            irSlot = 1;
            tabs.setCurrentTabIndex (2);
        }
        else
            nams.add (f);
    }
    if (! nams.isEmpty())
    {
        proc.importFiles (nams);
        tabs.setCurrentTabIndex (0);
    }
    repaint();
}
} // namespace namvx
