#include "TubePanel.h"

namespace namknobs
{
TubePanel::TubePanel (NamKnobsProcessor& p)
    : proc (p),
      preOn (p.getAPVTS(), pid::preTubeOn, "Enable"), paOn (p.getAPVTS(), pid::powerAmpOn, "Enable"),
      harmOn (p.getAPVTS(), pid::harmOn, "Enable"),
      preDrive (p.getAPVTS(), pid::preTubeDrive, "Drive"), preMix (p.getAPVTS(), pid::preTubeMix, "Mix"),
      tubeType (p.getAPVTS(), pid::tubeType),
      bias (p.getAPVTS(), pid::bias, "Bias"), sag (p.getAPVTS(), pid::sag, "Sag"), nfb (p.getAPVTS(), pid::nfb, "Feedback"),
      xfmr (p.getAPVTS(), pid::transformer, "Transformer"), mismatch (p.getAPVTS(), pid::mismatch, "Mismatch"),
      speaker (p.getAPVTS(), pid::speaker, "Speaker"),
      amount (p.getAPVTS(), pid::harmAmount, "Harmonics"), evenOdd (p.getAPVTS(), pid::harmEvenOdd, "Odd / Even"),
      touch (p.getAPVTS(), pid::harmTouch, "Touch"), attack (p.getAPVTS(), pid::harmAttack, "Pick Attack"),
      lowDef (p.getAPVTS(), pid::harmLowDef, "Low Def"), highDetail (p.getAPVTS(), pid::harmHighDetail, "High Detail"),
      loudness (p.getAPVTS(), pid::levelMatch, "Level match (every capture at the same loudness)")
{
    for (juce::Component* c : { (juce::Component*) &preOn, (juce::Component*) &paOn, (juce::Component*) &harmOn,
                                (juce::Component*) &preDrive, (juce::Component*) &preMix, (juce::Component*) &tubeType,
                                (juce::Component*) &bias, (juce::Component*) &sag, (juce::Component*) &nfb, (juce::Component*) &xfmr,
                                (juce::Component*) &mismatch, (juce::Component*) &speaker, (juce::Component*) &amount,
                                (juce::Component*) &evenOdd, (juce::Component*) &touch, (juce::Component*) &attack,
                                (juce::Component*) &lowDef, (juce::Component*) &highDetail, (juce::Component*) &osBox,
                                (juce::Component*) &maxActiveBox, (juce::Component*) &linearPhase, (juce::Component*) &alwaysOn,
                                (juce::Component*) &loudness, (juce::Component*) &loudnessLabel, (juce::Component*) &osLabel,
                                (juce::Component*) &maxLabel })
        addAndMakeVisible (c);

    osBox.addItem ("2x", 1); osBox.addItem ("4x", 2); osBox.addItem ("8x", 3);
    for (int i = 1; i <= 6; ++i) maxActiveBox.addItem (juce::String (i), i);
    for (auto* l : { &loudnessLabel, &osLabel, &maxLabel })
    {
        l->setFont (ui::semi (12.0f));
        l->setColour (juce::Label::textColourId, ui::textDim);
    }
    osBox.onChange = maxActiveBox.onChange = [this] { pushOptions(); };
    linearPhase.onClick = alwaysOn.onClick = [this] { pushOptions(); };
    refresh();
}

void TubePanel::refresh()
{
    const auto o = proc.getEngineOptions();
    osBox.setSelectedId (o.oversampling, juce::dontSendNotification);
    maxActiveBox.setSelectedId (o.maxActive, juce::dontSendNotification);
    linearPhase.setToggleState (o.linearPhase, juce::dontSendNotification);
    alwaysOn.setToggleState (o.alwaysOn, juce::dontSendNotification);
}

void TubePanel::pushOptions()
{
    auto o = proc.getEngineOptions();
    o.oversampling = std::max (1, osBox.getSelectedId());
    o.maxActive = std::max (1, maxActiveBox.getSelectedId());
    o.linearPhase = linearPhase.getToggleState();
    o.alwaysOn = alwaysOn.getToggleState();
    proc.setEngineOptions (o);
}

void TubePanel::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    ui::drawPanel (g, areas[0].toFloat(), "12AX7 preamp stage");
    ui::drawPanel (g, areas[1].toFloat(), "Power amp  -  push-pull, sag, transformer, feedback");
    ui::drawPanel (g, areas[2].toFloat(), "Harmonic engine");
    ui::drawPanel (g, areas[3].toFloat(), "Engine quality");
    g.setColour (ui::textDim);
    g.setFont (ui::body (11.5f));
    g.drawFittedText ("Add-ons that complement your captures. Off by default for captures of full amps; "
                      "use the power amp with preamp-only captures. Master drives the power stage; Presence and "
                      "Resonance act inside its feedback loop.",
                      areas[3].reduced (16, 12).withTop (alwaysOn.getBottom() + 18), juce::Justification::topLeft, 6);
}

void TubePanel::resized()
{
    auto r = getLocalBounds().reduced (24, 18);
    auto right = r.removeFromRight (300);
    r.removeFromRight (16);
    auto top = r.removeFromTop (r.getHeight() / 2 - 8);
    r.removeFromTop (16);
    areas[0] = top.removeFromLeft (250);
    top.removeFromLeft (16);
    areas[1] = top;
    areas[2] = r;
    areas[3] = right;

    auto layoutKnobs = [] (juce::Rectangle<int> a, std::initializer_list<juce::Component*> ks) {
        const int w = std::min (100, a.getWidth() / (int) ks.size());
        for (auto* k : ks) k->setBounds (a.removeFromLeft (w));
    };
    {
        auto a = areas[0].reduced (16, 12); a.removeFromTop (24);
        preOn.setBounds (a.removeFromTop (30).removeFromLeft (120));
        layoutKnobs (a.withTrimmedTop (6), { &preDrive, &preMix });
    }
    {
        auto a = areas[1].reduced (16, 12); a.removeFromTop (24);
        auto row = a.removeFromTop (30);
        paOn.setBounds (row.removeFromLeft (120));
        tubeType.setBounds (row.removeFromLeft (100).reduced (0, 1));
        layoutKnobs (a.withTrimmedTop (6), { &bias, &sag, &nfb, &xfmr, &mismatch, &speaker });
    }
    {
        auto a = areas[2].reduced (16, 12); a.removeFromTop (24);
        harmOn.setBounds (a.removeFromTop (30).removeFromLeft (120));
        layoutKnobs (a.withTrimmedTop (6), { &amount, &evenOdd, &touch, &attack, &lowDef, &highDetail });
    }
    {
        auto a = areas[3].reduced (16, 12); a.removeFromTop (28);
        auto row = a.removeFromTop (30);
        osLabel.setBounds (row.removeFromLeft (150));
        osBox.setBounds (row.reduced (0, 1));
        a.removeFromTop (6);
        linearPhase.setBounds (a.removeFromTop (28));
        a.removeFromTop (6);
        row = a.removeFromTop (30);
        maxLabel.setBounds (row.removeFromLeft (150));
        maxActiveBox.setBounds (row.reduced (0, 1));
        a.removeFromTop (6);
        loudnessLabel.setBounds (a.removeFromTop (20));
        loudness.setBounds (a.removeFromTop (28));
        a.removeFromTop (4);
        a.removeFromTop (6);
        alwaysOn.setBounds (a.removeFromTop (44));
    }
}
} // namespace namknobs
