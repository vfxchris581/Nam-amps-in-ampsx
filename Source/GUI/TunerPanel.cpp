#include "TunerPanel.h"
#include "AudioEngine/PitchDetector.h"

namespace namknobs
{
namespace
{
    struct GuitarString { const char* label; int midi; };
    const GuitarString kStrings[6] = { { "E", 40 }, { "A", 45 }, { "D", 50 }, { "G", 55 }, { "B", 59 }, { "E", 64 } };
    constexpr double kInTune = 2.0; // cents
}

TunerPanel::TunerPanel (NamKnobsProcessor& p) : proc (p)
{
    mute.setToggleState (true, juce::dontSendNotification);
    mute.onClick = [this] { proc.getChain().tunerMute = mute.getToggleState(); };
    done.onClick = [this] { if (onClose) onClose(); };
    refDown.onClick = [this] { a4 = std::max (415.0, a4 - 1.0); repaint(); };
    refUp.onClick = [this] { a4 = std::min (466.0, a4 + 1.0); repaint(); };
    for (juce::Component* c : { (juce::Component*) &mute, (juce::Component*) &done, (juce::Component*) &refDown, (juce::Component*) &refUp })
        addAndMakeVisible (c);
    setInterceptsMouseClicks (true, true);
}

TunerPanel::~TunerPanel()
{
    proc.getChain().tunerActive = false;
}

void TunerPanel::visibilityChanged()
{
    auto& ch = proc.getChain();
    if (isVisible())
    {
        ch.tunerMute = mute.getToggleState();
        ch.tunerActive = true;
        filled = 0; hasNote = false; shownMidi = -1;
        startTimerHz (30);
    }
    else
    {
        ch.tunerActive = false;
        stopTimer();
    }
}

void TunerPanel::timerCallback()
{
    auto& ch = proc.getChain();
    float tmp[4096];
    for (;;)
    {
        const int got = ch.readTunerSamples (tmp, 4096);
        if (got <= 0) break;
        for (int i = 0; i < got; ++i) { ring[(size_t) writePos] = tmp[i]; writePos = (writePos + 1) % (int) ring.size(); }
        filled = std::min ((int) ring.size(), filled + got);
    }
    const double sr = ch.getSampleRate();
    const int need = std::clamp (PitchDetector::samplesNeeded (sr), 2048, (int) ring.size());
    if (filled < need) { update (0.0, 0.0); return; }
    window.resize ((size_t) need);
    for (int i = 0; i < need; ++i)
        window[(size_t) i] = ring[(size_t) ((writePos - need + i + (int) ring.size()) % (int) ring.size())];
    const auto r = PitchDetector::detect (window.data(), need, sr);
    update (r.hz, r.clarity);
}

void TunerPanel::analyse (const float* x, int n, double sampleRate)
{
    const auto r = PitchDetector::detect (x, n, sampleRate);
    hasNote = false; shownMidi = -1;
    update (r.hz, r.clarity);
}

void TunerPanel::update (double hz, double clarity)
{
    if (hz > 0.0 && clarity > 0.75)
    {
        silentTicks = 0;
        const auto nt = PitchDetector::noteFor (hz, a4);
        if (nt.midi != shownMidi || ! hasNote) { shownMidi = nt.midi; shownCents = nt.cents; shownHz = hz; }
        else
        {
            shownCents += 0.4 * (nt.cents - shownCents); // steady needle
            shownHz += 0.4 * (hz - shownHz);
        }
        hasNote = true;
        repaint();
    }
    else if (hasNote && ++silentTicks > 15) // hold the last reading for half a second
    {
        hasNote = false;
        repaint();
    }
}

void TunerPanel::paint (juce::Graphics& g)
{
    ui::drawTolex (g, getLocalBounds().toFloat());
    ui::drawPanel (g, card.toFloat(), "Tuner");

    const bool inTune = hasNote && std::abs (shownCents) <= kInTune;
    const auto noteColour = ! hasNote ? ui::textDim.withAlpha (0.5f) : (inTune ? ui::green : ui::text);

    // ---- note name ----------------------------------------------------------------------------
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto na = noteArea.toFloat();
    if (hasNote)
    {
        const juce::String full (names[((shownMidi % 12) + 12) % 12]);
        const auto letter = full.substring (0, 1), sharp = full.substring (1);
        const auto big = ui::brand (150.0f);
        const float lw = juce::GlyphArrangement::getStringWidth (big, letter);
        const float cx = na.getCentreX() - 20.0f;
        g.setColour (noteColour);
        g.setFont (big);
        g.drawText (letter, juce::Rectangle<float> (cx - lw * 0.5f, na.getY(), lw + 4, na.getHeight()), juce::Justification::centred);
        g.setFont (ui::brand (58.0f));
        if (sharp.isNotEmpty())
            g.drawText (juce::String::fromUTF8 ("\xe2\x99\xaf"), juce::Rectangle<float> (cx + lw * 0.5f + 2, na.getY() + 24, 60, 70), juce::Justification::centredLeft);
        g.setColour (noteColour.withAlpha (0.7f));
        g.setFont (ui::semi (34.0f));
        g.drawText (juce::String (shownMidi / 12 - 1), juce::Rectangle<float> (cx + lw * 0.5f + 6, na.getBottom() - 74, 60, 40), juce::Justification::centredLeft);

        // flat / sharp arrows
        const bool flat = shownCents < -kInTune, sharpOn = shownCents > kInTune;
        auto arrow = [&] (juce::Rectangle<float> r, bool left, bool lit) {
            juce::Path p;
            if (left) p.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getCentreY());
            else      p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
            g.setColour (lit ? ui::amber : ui::line);
            g.fillPath (p);
        };
        arrow (juce::Rectangle<float> (34, 44).withCentre ({ na.getCentreX() - 230.0f, na.getCentreY() }), true, flat);
        arrow (juce::Rectangle<float> (34, 44).withCentre ({ na.getCentreX() + 230.0f, na.getCentreY() }), false, sharpOn);
    }
    else
    {
        g.setColour (noteColour);
        g.setFont (ui::brand (110.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94"), na, juce::Justification::centred);
    }

    // ---- cents meter ------------------------------------------------------------------------------
    auto m = meterArea.toFloat();
    const float cx = m.getCentreX(), half = m.getWidth() * 0.5f;
    auto xFor = [&] (double cents) { return cx + (float) (juce::jlimit (-50.0, 50.0, cents) / 50.0) * half; };
    // in-tune window
    g.setColour (ui::green.withAlpha (0.12f));
    g.fillRoundedRectangle (juce::Rectangle<float> (xFor (-kInTune), m.getY(), xFor (kInTune) - xFor (-kInTune), 64), 3.0f);
    // segment strip
    const int nSeg = 51;
    const float segW = m.getWidth() / nSeg;
    for (int s = 0; s < nSeg; ++s)
    {
        const double c = -50.0 + 100.0 * s / (nSeg - 1);
        const auto seg = juce::Rectangle<float> (m.getX() + s * segW + 1.5f, m.getY() + 8, segW - 3.0f, 26);
        bool lit = false;
        juce::Colour col = std::abs (c) <= kInTune ? ui::green : (std::abs (c) < 15 ? ui::amber : ui::red);
        if (hasNote)
        {
            const double lo = std::min (0.0, shownCents), hi = std::max (0.0, shownCents);
            lit = (c >= lo - 1.0 && c <= hi + 1.0);
            if (inTune) { lit = std::abs (c) <= 4.0; col = ui::green; }
        }
        g.setColour (lit ? col : ui::panelHi);
        g.fillRoundedRectangle (seg, 2.0f);
    }
    // scale
    g.setFont (ui::semi (11.5f));
    for (int c = -50; c <= 50; c += 5)
    {
        const float x = xFor (c);
        const bool major = c % 25 == 0;
        g.setColour (major ? ui::text.withAlpha (0.8f) : ui::textDim.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, m.getY() + 40, m.getY() + (major ? 54.0f : 48.0f));
        if (major)
            g.drawText (c == 0 ? juce::String ("0") : (c > 0 ? "+" : "") + juce::String (c), juce::Rectangle<float> (x - 24, m.getY() + 56, 48, 16), juce::Justification::centred);
    }
    // needle
    if (hasNote)
    {
        const float x = xFor (shownCents);
        juce::Path needle;
        needle.addTriangle (x - 9, m.getY() - 6, x + 9, m.getY() - 6, x, m.getY() + 6);
        g.setColour (inTune ? ui::green : ui::gold);
        g.fillPath (needle);
        g.fillRect (x - 1.0f, m.getY() + 4, 2.0f, 34.0f);
    }

    // read-out line
    g.setFont (ui::semi (15.0f));
    g.setColour (ui::text.withAlpha (hasNote ? 0.9f : 0.5f));
    const auto readout = hasNote ? juce::String (shownHz, 2) + " Hz   " + juce::String::fromUTF8 ("\xc2\xb7") + "   "
                                     + (shownCents >= 0 ? "+" : "") + juce::String (shownCents, 1) + " cents"
                                     + (inTune ? juce::String ("   ") + juce::String::fromUTF8 ("\xc2\xb7") + "   IN TUNE" : juce::String())
                                 : juce::String ("Play a single open string");
    g.drawText (readout, meterArea.toFloat().withY (m.getY() + 84).withHeight (24), juce::Justification::centred);

    // ---- strings of a standard-tuned guitar ------------------------------------------------------
    auto sa = stringsArea.toFloat();
    g.setFont (ui::semi (10.5f).withExtraKerningFactor (0.2f));
    g.setColour (ui::textDim);
    g.drawText ("STANDARD TUNING", sa.removeFromTop (16), juce::Justification::centred);
    sa.removeFromTop (6);
    const float chipW = 64, gap = 12, total = 6 * chipW + 5 * gap;
    float x = sa.getCentreX() - total * 0.5f;
    for (auto& s : kStrings)
    {
        const auto chip = juce::Rectangle<float> (x, sa.getY(), chipW, 40);
        const bool match = hasNote && shownMidi == s.midi;
        g.setColour (match ? (inTune ? ui::green : ui::gold).withAlpha (0.22f) : ui::panelHi);
        g.fillRoundedRectangle (chip, 6.0f);
        g.setColour (match ? (inTune ? ui::green : ui::gold) : ui::line);
        g.drawRoundedRectangle (chip, 6.0f, match ? 1.6f : 1.0f);
        g.setColour (match ? ui::text : ui::textDim);
        g.setFont (ui::brand (17.0f));
        g.drawText (juce::String (s.label) + juce::String (s.midi / 12 - 1), chip, juce::Justification::centred);
        x += chipW + gap;
    }

    // reference pitch
    g.setColour (ui::textDim);
    g.setFont (ui::semi (13.0f));
    g.drawText ("A4 = " + juce::String ((int) a4) + " Hz", refArea.toFloat(), juce::Justification::centred);
}

void TunerPanel::resized()
{
    card = getLocalBounds().reduced (24, 16);
    auto r = card.reduced (24, 14);
    auto head = r.removeFromTop (30);
    done.setBounds (head.removeFromRight (90));
    head.removeFromRight (16);
    mute.setBounds (head.removeFromRight (240));

    r.removeFromTop (6);
    noteArea = r.removeFromTop (190).withSizeKeepingCentre (520, 190);
    r.removeFromTop (22);
    meterArea = r.removeFromTop (110).withSizeKeepingCentre (std::min (820, r.getWidth() - 40), 110);
    r.removeFromTop (16);
    stringsArea = r.removeFromTop (64).withSizeKeepingCentre (500, 64);
    r.removeFromTop (14);
    auto ref = r.removeFromTop (30).withSizeKeepingCentre (240, 30);
    refDown.setBounds (ref.removeFromLeft (34));
    refUp.setBounds (ref.removeFromRight (34));
    refArea = ref;
}
} // namespace namknobs
