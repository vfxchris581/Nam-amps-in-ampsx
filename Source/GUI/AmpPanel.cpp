#include "AmpPanel.h"
#include <set>

namespace namknobs
{
AmpPanel::AmpPanel (NamKnobsProcessor& p)
    : proc (p),
      input (p.getAPVTS(), pid::input, "Input"), output (p.getAPVTS(), pid::output, "Output"),
      gateThreshold (p.getAPVTS(), pid::gateThreshold, "Threshold"), gateRelease (p.getAPVTS(), pid::gateRelease, "Release"),
      gateOn (p.getAPVTS(), pid::gateOn, "Noise Gate"),
      tube (p.getAPVTS(), pid::preTubeOn, "12AX7 Stage"), power (p.getAPVTS(), pid::powerAmpOn, "Power Amp"),
      harm (p.getAPVTS(), pid::harmOn, "Harmonics"), cab (p.getAPVTS(), pid::cabOn, "Cabinet")
{
    for (int k = 0; k < kNumControls; ++k)
    {
        knobs.push_back (std::make_unique<Knob> (p.getAPVTS(), controlId ((Control) k), controlName ((Control) k), true));
        addAndMakeVisible (*knobs.back());
    }
    for (auto* c : { &input, &output, &gateThreshold, &gateRelease }) addAndMakeVisible (*c);
    addAndMakeVisible (gateOn);
    gateOn.setTooltip ("Silences hiss and hum between notes. It listens to your guitar before the amp "
                       "and mutes after it, so picks are never cut and chords ring out.");
    gateThreshold.slider.setTooltip ("Raise until the hiss is gone when you stop playing; lower if note tails get cut.");
    gateRelease.slider.setTooltip ("How quickly the gate closes once a note has died away.");
    for (int i = 0; i < AmpDefinition::kMaxBlendModels; ++i)
    {
        mixKnobs.push_back (std::make_unique<Knob> (p.getAPVTS(), pid::blendMix[i], "Model " + juce::String (i + 1)));
        addChildComponent (*mixKnobs.back());
    }
    for (auto* t : { &tube, &power, &harm, &cab }) addAndMakeVisible (*t);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    blendInfo.setFont (ui::body (12.5f));
    blendInfo.setColour (juce::Label::textColourId, ui::textDim);
    blendInfo.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (blendInfo);
    refresh();
    startTimerHz (20);
}

void AmpPanel::refresh()
{
    const auto* amp = proc.getCurrentAmp();
    ampName = amp ? juce::String (amp->name) : juce::String ("NO AMP LOADED");
    manufacturer = amp ? juce::String (amp->manufacturer) : juce::String ("Drop .nam captures anywhere on this window");

    const int nCh = amp ? (int) amp->channels.size() : 0;
    if (nCh != lastChannelCount || amp)
    {
        channelButtons.clear();
        for (int c = 0; c < nCh; ++c)
        {
            auto* b = channelButtons.add (new juce::TextButton (amp->channels[(size_t) c].name));
            b->getProperties().set ("channel", true);
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::textColourOffId, ui::textDim);
            b->setColour (juce::TextButton::textColourOnId, ui::amber);
            b->onClick = [this, c] {
                if (auto* prm = proc.getAPVTS().getParameter (pid::channel))
                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) c));
            };
            addAndMakeVisible (b);
        }
        lastChannelCount = nCh;
    }

    // Blend amps: one mix knob per model, captioned with the model's name.
    numMix = 0;
    if (amp && amp->isBlend && ! amp->channels.empty())
    {
        int i = 0;
        for (auto& cap : amp->channels[0].captures)
        {
            if (! cap.enabled || i >= AmpDefinition::kMaxBlendModels) continue;
            auto label = juce::String (cap.meta.name.empty() ? cap.meta.fileName : cap.meta.name);
            if (label.length() > 22) label = label.substring (0, 21) + "...";
            mixKnobs[(size_t) i]->setCaption (label);
            ++i;
        }
        numMix = i;
    }
    if (numMix > 6) numMix = 0; // many loaders: the Tone Matrix page is the place for them
    for (int i = 0; i < (int) mixKnobs.size(); ++i) mixKnobs[(size_t) i]->setVisible (i < numMix);

    // Badge each control: NAM = realised by blending captures, DSP = realised by circuit/EQ.
    for (int k = 0; k < kNumControls; ++k)
    {
        const auto c = (Control) k;
        bool covered = false;
        if (amp && ! amp->channels.empty())
        {
            const auto& ch = amp->channels[(size_t) proc.getCurrentChannel()];
            std::set<long> vals;
            for (auto& cap : ch.captures)
                if (cap.enabled && isKnown (cap.coords[(size_t) k])) vals.insert (std::lround (cap.coords[(size_t) k] * 100));
            covered = vals.size() >= 2;
        }
        if (c == Control::Volume || c == Control::Master)
            knobs[(size_t) k]->setBadge (covered ? "NAM" : "", ui::green);
        else
            knobs[(size_t) k]->setBadge (covered ? "NAM" : "DSP", covered ? ui::green : ui::textDim);
    }
    resized();
    repaint();
}

juce::Rectangle<int> AmpPanel::plateBounds() const
{
    return getLocalBounds().reduced (24, 0).withTrimmedTop (20).withHeight (330);
}

void AmpPanel::paint (juce::Graphics& g)
{
    ui::drawTolex (g, getLocalBounds().toFloat());
    const auto plate = plateBounds().toFloat();
    ui::drawFaceplate (g, plate);

    // name block
    auto head = plate.reduced (34, 22).removeFromTop (54);
    if (channelButtons.size() > 0) head.setRight ((float) channelButtons.getFirst()->getX() - 20.0f);
    g.setColour (ui::ink);
    g.setFont (ui::brand (30.0f).withExtraKerningFactor (0.06f));
    g.drawFittedText (ampName.toUpperCase(), head.removeFromTop (34).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    g.setColour (ui::ink.withAlpha (0.6f));
    g.setFont (ui::semi (12.0f).withExtraKerningFactor (0.22f));
    g.drawText (manufacturer.toUpperCase(), head, juce::Justification::centredLeft);

    // pilot jewel
    const auto jewel = juce::Rectangle<float> (22, 22).withCentre ({ plate.getRight() - 52.0f, plate.getY() + 46.0f });
    ui::drawLed (g, jewel, ui::red, proc.getCurrentAmp() != nullptr);
    g.setColour (ui::ink.withAlpha (0.6f));
    g.setFont (ui::semi (9.5f).withExtraKerningFactor (0.2f));
    g.drawText ("POWER", jewel.withY (jewel.getBottom() + 4).withSizeKeepingCentre (60, 12), juce::Justification::centred);

    // groove between header and knobs
    g.setColour (ui::ink.withAlpha (0.25f));
    g.drawHorizontalLine ((int) plate.getY() + 98, plate.getX() + 24, plate.getRight() - 24);
    if (channelButtons.size() > 0)
    {
        g.setColour (ui::ink.withAlpha (0.55f));
        g.setFont (ui::semi (10.0f).withExtraKerningFactor (0.2f));
        const auto first = channelButtons.getFirst()->getBounds().toFloat();
        g.drawText ("CHANNEL", juce::Rectangle<float> (first.getX(), plate.getY() + 18, 200, 12), juce::Justification::centredLeft);
    }

    // lower strip
    auto strip = getLocalBounds().toFloat().withTrimmedTop (plate.getBottom() + 18).reduced (24, 0).withTrimmedBottom (18);
    ui::drawPanel (g, strip);

    // noise gate box with its activity LED
    if (! gateArea.isEmpty())
    {
        const auto ga = gateArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (ga, 6.0f);
        g.setColour (ui::gold.withAlpha (0.25f));
        g.drawRoundedRectangle (ga, 6.0f, 1.0f);
        const auto led = juce::Rectangle<float> (10, 10).withCentre ({ ga.getRight() - 16.0f, ga.getY() + 19.0f });
        ui::drawLed (g, led, ui::green, gateLed > 0.5f);
    }
    g.setColour (ui::textDim);
    g.setFont (ui::semi (10.5f).withExtraKerningFactor (0.2f));
    g.drawText (numMix > 0 ? juce::String::fromUTF8 ("SIGNAL CHAIN  \xc2\xb7  MODEL BLEND") : juce::String ("SIGNAL CHAIN"), strip.reduced (150, 12).removeFromTop (14), juce::Justification::centred);
}

void AmpPanel::resized()
{
    const auto plate = plateBounds();
    // channel selector, centred at the top of the faceplate
    if (channelButtons.size() > 0)
    {
        const int bw = 96, gap = 8, total = channelButtons.size() * bw + (channelButtons.size() - 1) * gap;
        int x = plate.getRight() - 110 - total;
        for (auto* b : channelButtons) { b->setBounds (x, plate.getY() + 36, bw, 34); x += bw + gap; }
    }
    auto row = plate.reduced (30, 0).withTrimmedTop (118).withTrimmedBottom (22);
    const int kw = row.getWidth() / kNumControls;
    for (int k = 0; k < kNumControls; ++k)
        knobs[(size_t) k]->setBounds (row.removeFromLeft (kw).reduced (4, 0));

    auto strip = getLocalBounds().withTrimmedTop (plate.getBottom() + 18).reduced (24, 0).withTrimmedBottom (18).reduced (18, 14);
    inMeter.setBounds (strip.removeFromLeft (8).reduced (0, 6));
    strip.removeFromLeft (8);
    input.setBounds (strip.removeFromLeft (96));
    strip.removeFromLeft (14);
    gateArea = strip.removeFromLeft (196).withTrimmedBottom (0);
    {
        auto ga = gateArea.reduced (8, 6);
        gateOn.setBounds (ga.removeFromTop (28).withTrimmedRight (20));
        ga.removeFromTop (2);
        gateThreshold.setBounds (ga.removeFromLeft (ga.getWidth() / 2));
        gateRelease.setBounds (ga);
    }
    outMeter.setBounds (strip.removeFromRight (8).reduced (0, 6));
    strip.removeFromRight (8);
    output.setBounds (strip.removeFromRight (96));
    strip.reduce (28, 0);
    auto toggles = strip.removeFromTop (numMix > 0 ? 54 : strip.getHeight() / 2).withTrimmedTop (14);
    if (numMix > 0)
    {
        auto mixRow = strip.removeFromTop (strip.getHeight() - 34);
        const int w = std::min (170, mixRow.getWidth() / numMix);
        mixRow = mixRow.withSizeKeepingCentre (w * numMix, mixRow.getHeight());
        for (int i = 0; i < numMix; ++i) mixKnobs[(size_t) i]->setBounds (mixRow.removeFromLeft (w));
    }
    const int tw = toggles.getWidth() / 4;
    for (auto* t : { &tube, &power, &harm, &cab }) t->setBounds (toggles.removeFromLeft (tw).withSizeKeepingCentre (tw - 8, 30));
    blendInfo.setBounds (strip.withTrimmedBottom (10));
}

void AmpPanel::timerCallback()
{
    auto& ch = proc.getChain();
    inMeter.setLevel (ch.inputPeak.load());
    const bool gateEnabled = proc.getAPVTS().getRawParameterValue (pid::gateOn)->load() > 0.5f;
    const float led = gateEnabled ? ch.gateGain.load() : 0.0f;
    if (std::abs (led - gateLed) > 0.05f) { gateLed = led; repaint (gateArea.withTrimmedLeft (gateArea.getWidth() - 30).withHeight (34)); }
    gateThreshold.setAlpha (gateEnabled ? 1.0f : 0.45f);
    gateRelease.setAlpha (gateEnabled ? 1.0f : 0.45f);
    outMeter.setLevel (ch.outputPeak.load());
    juce::String info;
    if (const auto* amp = proc.getCurrentAmp())
    {
        const int c = proc.getCurrentChannel();
        const int n = c < (int) amp->channels.size() ? (int) amp->channels[(size_t) c].captures.size() : 0;
        if (amp->isBlend) info << "Model blend: " << ch.runningModels.load() << " of " << n << " models playing";
        else info << "Channel " << (c + 1) << ": blending " << ch.runningModels.load() << " of " << n << " NAM captures";
        info << juce::String::fromUTF8 ("  \xc2\xb7  latency ") << ch.getLatencySamples() << " samples  \xc2\xb7  " << proc.getStatus();
    }
    else
        info = proc.getStatus();
    if (blendInfo.getText() != info) blendInfo.setText (info, juce::dontSendNotification);
    for (int i = 0; i < channelButtons.size(); ++i)
        channelButtons[i]->setToggleState (i == proc.getCurrentChannel(), juce::dontSendNotification);
    repaint (plateBounds().withWidth (120).withX (plateBounds().getRight() - 120).withHeight (90));
}
} // namespace namknobs
