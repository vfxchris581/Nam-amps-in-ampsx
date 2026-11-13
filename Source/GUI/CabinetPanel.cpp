#include "CabinetPanel.h"

namespace namknobs
{
CabinetPanel::Slot::Slot (NamKnobsProcessor& p, int i)
{
    auto& s = p.getAPVTS();
    on = std::make_unique<Toggle> (s, i == 0 ? pid::ir1On : pid::ir2On, "On");
    invert = std::make_unique<Toggle> (s, i == 0 ? pid::ir1Invert : pid::ir2Invert, juce::String::fromUTF8 ("Polarity \xc3\x98"));
    invert->getProperties().set ("ledRed", true);
    level = std::make_unique<Knob> (s, i == 0 ? pid::ir1Level : pid::ir2Level, "Volume");
    pan = std::make_unique<Knob> (s, i == 0 ? pid::ir1Pan : pid::ir2Pan, "Pan");
    delay = std::make_unique<Knob> (s, i == 0 ? pid::ir1Delay : pid::ir2Delay, "Delay");
    for (auto* l : { &file, &info })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setColour (juce::Label::textColourId, l == &file ? ui::text : ui::textDim);
        l->setFont (l == &file ? ui::semi (14.0f) : ui::body (12.0f));
    }
}

CabinetPanel::CabinetPanel (NamKnobsProcessor& p)
    : proc (p), s1 (p, 0), s2 (p, 1),
      blend (p.getAPVTS(), pid::cabBlend, "IR Blend"), level (p.getAPVTS(), pid::cabLevel, "Cab Level"),
      stereo (p.getAPVTS(), pid::cabStereo, "Stereo"), autoAlign (p.getAPVTS(), pid::cabAutoAlign, "Auto phase align")
{
    int idx = 0;
    for (auto* s : { &s1, &s2 })
    {
        for (juce::Component* c : { (juce::Component*) s->on.get(), (juce::Component*) s->invert.get(), (juce::Component*) s->level.get(),
                                    (juce::Component*) s->pan.get(), (juce::Component*) s->delay.get(), (juce::Component*) &s->load,
                                    (juce::Component*) &s->clear, (juce::Component*) &s->file, (juce::Component*) &s->info })
            addAndMakeVisible (c);
        s->load.onClick = [this, idx] {
            const juce::File current (proc.getIRPath (idx));
            chooser = std::make_unique<juce::FileChooser> ("Load cabinet IR", current.existsAsFile() ? current.getParentDirectory() : juce::File(),
                                                           "*.wav;*.WAV;*.wave;*.aif;*.AIF;*.aiff;*.AIFF");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, idx] (const juce::FileChooser& fc) {
                                      if (fc.getResult().existsAsFile())
                                      {
                                          const auto err = proc.loadImpulseResponse (idx, fc.getResult());
                                          if (err.isNotEmpty())
                                              juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Impulse response", err);
                                          refresh();
                                      }
                                  });
        };
        s->clear.onClick = [this, idx] { proc.clearImpulseResponse (idx); refresh(); };
        ++idx;
    }
    for (juce::Component* c : { (juce::Component*) &blend, (juce::Component*) &level, (juce::Component*) &stereo,
                                (juce::Component*) &autoAlign, (juce::Component*) &alignInfo, (juce::Component*) &cabPresets,
                                (juce::Component*) &saveCab })
        addAndMakeVisible (c);
    alignInfo.setFont (ui::body (12.0f));
    alignInfo.setColour (juce::Label::textColourId, ui::textDim);
    alignInfo.setJustificationType (juce::Justification::topLeft);

    cabPresets.setTextWhenNothingSelected ("Cab presets...");
    cabPresets.onChange = [this] {
        const int i = cabPresets.getSelectedItemIndex();
        const auto files = proc.getPresets().listCabPresets();
        if (i < 0 || i >= files.size()) return;
        PresetManager::CabPreset cp;
        if (! proc.getPresets().loadCabPreset (files[i], cp)) return;
        if (cp.ir1Path.isNotEmpty()) proc.loadImpulseResponse (0, juce::File (cp.ir1Path)); else proc.clearImpulseResponse (0);
        if (cp.ir2Path.isNotEmpty()) proc.loadImpulseResponse (1, juce::File (cp.ir2Path)); else proc.clearImpulseResponse (1);
        for (auto& nv : cp.values)
            if (auto* prm = proc.getAPVTS().getParameter (nv.name.toString()))
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (double) nv.value));
        refresh();
    };
    saveCab.onClick = [this] {
        auto* w = new juce::AlertWindow ("Save cabinet preset", "Name this IR combination:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", "My Cab");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
            if (r == 1)
            {
                PresetManager::CabPreset cp;
                cp.name = w->getTextEditorContents ("name");
                cp.ir1Path = proc.getIRPath (0);
                cp.ir2Path = proc.getIRPath (1);
                for (auto* id : { pid::cabBlend, pid::cabLevel, pid::cabStereo, pid::cabAutoAlign, pid::ir1On, pid::ir1Level,
                                  pid::ir1Pan, pid::ir1Invert, pid::ir1Delay, pid::ir2On, pid::ir2Level, pid::ir2Pan,
                                  pid::ir2Invert, pid::ir2Delay })
                    cp.values.set (id, (double) proc.getAPVTS().getRawParameterValue (id)->load());
                proc.getPresets().saveCabPreset (cp);
                refresh();
            }
        }), true);
    };
    refresh();
}

void CabinetPanel::refresh()
{
    int i = 0;
    for (auto* s : { &s1, &s2 })
    {
        const auto& info = proc.getChain().getCabinet().slot (i).getInfo();
        if (info.loaded())
        {
            s->file.setText (info.file.getFileNameWithoutExtension().isEmpty() ? juce::String ("(impulse)") : info.file.getFileNameWithoutExtension(),
                             juce::dontSendNotification);
            s->info.setText (juce::String (info.sampleRate / 1000.0, 1) + " kHz  \xc2\xb7  " + (info.numChannels == 2 ? "stereo" : "mono")
                                 + "  \xc2\xb7  " + juce::String (1000.0 * info.lengthSamples / info.sampleRate, 0) + " ms  \xc2\xb7  onset "
                                 + juce::String (info.onsetMs, 2) + " ms",
                             juce::dontSendNotification);
        }
        else
        {
            s->file.setText ("No IR loaded", juce::dontSendNotification);
            s->info.setText ("WAV / AIFF, mono or stereo, 44.1 - 192 kHz", juce::dontSendNotification);
        }
        ++i;
    }
    const auto al = proc.getChain().getCabinet().getAlignment();
    juce::String a;
    if (proc.getChain().getCabinet().slot (0).isLoaded() && proc.getChain().getCabinet().slot (1).isLoaded())
    {
        if (std::abs (al.correlation) < 0.3f)
            a << "IRs are too different to time-align automatically (correlation "
              << juce::String (std::abs (al.correlation), 2) << "). Use the Delay knobs by ear.";
        else
            a << "Phase alignment: IR 1 +" << juce::String (al.slot1DelayMs, 3) << " ms, IR 2 +" << juce::String (al.slot2DelayMs, 3)
              << " ms\nCorrelation " << juce::String (std::abs (al.correlation), 2);
        if (al.polarityMismatch) a << "\nIRs are out of polarity: try Polarity on IR 2";
    }
    else
        a = "Load two IRs to blend two cabinets or two microphones.";
    alignInfo.setText (a, juce::dontSendNotification);

    cabPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (auto& f : proc.getPresets().listCabPresets()) cabPresets.addItem (f.getFileNameWithoutExtension(), id++);
    repaint();
}

void CabinetPanel::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    ui::drawPanel (g, slotArea[0].toFloat(), "IR slot 1");
    ui::drawPanel (g, slotArea[1].toFloat(), "IR slot 2");
    ui::drawPanel (g, globalArea.toFloat(), "Cabinet");
}

void CabinetPanel::resized()
{
    auto r = getLocalBounds().reduced (24, 18);
    globalArea = r.removeFromRight (290);
    r.removeFromRight (16);
    slotArea[0] = r.removeFromTop ((r.getHeight() - 16) / 2);
    r.removeFromTop (16);
    slotArea[1] = r;

    int i = 0;
    for (auto* s : { &s1, &s2 })
    {
        auto a = slotArea[i++].reduced (16, 12);
        a.removeFromTop (22);
        auto top = a.removeFromTop (30);
        s->load.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        s->clear.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (16);
        s->file.setBounds (top);
        s->info.setBounds (a.removeFromTop (22).withTrimmedLeft (194));
        a.removeFromTop (6);
        auto toggles = a.removeFromLeft (150);
        s->on->setBounds (toggles.removeFromTop (30));
        s->invert->setBounds (toggles.removeFromTop (30));
        const int kw = std::min (110, a.getWidth() / 3);
        for (auto* k : { s->level.get(), s->pan.get(), s->delay.get() }) k->setBounds (a.removeFromLeft (kw));
    }

    auto gA = globalArea.reduced (16, 12);
    gA.removeFromTop (26);
    auto knobs = gA.removeFromTop (130);
    blend.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
    level.setBounds (knobs);
    gA.removeFromTop (8);
    stereo.setBounds (gA.removeFromTop (30));
    autoAlign.setBounds (gA.removeFromTop (30));
    gA.removeFromTop (6);
    alignInfo.setBounds (gA.removeFromTop (70));
    gA.removeFromTop (10);
    cabPresets.setBounds (gA.removeFromTop (32));
    gA.removeFromTop (8);
    saveCab.setBounds (gA.removeFromTop (32));
}
} // namespace namknobs
