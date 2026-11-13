#include "EqPanel.h"

namespace namvx
{
namespace
{
constexpr double kMinF = 20.0, kMaxF = 20000.0, kRangeDb = 18.0;
const juce::Colour bandColours[7] = { juce::Colour (0xff8fb4ff), juce::Colour (0xff6fd3c7), juce::Colour (0xffffb648),
                                      juce::Colour (0xfff07f5a), juce::Colour (0xffd98cf0), juce::Colour (0xff9fd36f),
                                      juce::Colour (0xff8fb4ff) };
bool isCut (int b) { return b == EqSettings::LowCut || b == EqSettings::HighCut; }
} // namespace

EqPanel::EqPanel (NamVXProcessor& p)
    : proc (p), eqOn (p.getAPVTS(), pid::eqOn, "EQ on"), position (p.getAPVTS(), pid::eqPost),
      output (p.getAPVTS(), pid::eqOut, "Output")
{
    addAndMakeVisible (eqOn);
    addAndMakeVisible (position);
    addAndMakeVisible (output);
    for (int b = 0; b < 7; ++b)
    {
        auto& ui = bands[(size_t) b];
        ui.on = std::make_unique<Toggle> (p.getAPVTS(), pid::eqBand[b][0], pid::eqBandName[b]);
        ui.freq = std::make_unique<Knob> (p.getAPVTS(), pid::eqBand[b][1], "Freq");
        addAndMakeVisible (*ui.on);
        addAndMakeVisible (*ui.freq);
        if (*pid::eqBand[b][2]) { ui.gain = std::make_unique<Knob> (p.getAPVTS(), pid::eqBand[b][2], "Gain"); addAndMakeVisible (*ui.gain); }
        if (*pid::eqBand[b][3]) { ui.q = std::make_unique<Knob> (p.getAPVTS(), pid::eqBand[b][3], "Q"); addAndMakeVisible (*ui.q); }
        if (*pid::eqBand[b][4]) { ui.slope = std::make_unique<Choice> (p.getAPVTS(), pid::eqBand[b][4]); addAndMakeVisible (*ui.slope); }
    }
    startTimerHz (30);
}

EqPanel::~EqPanel() { stopTimer(); }

EqSettings EqPanel::currentSettings() const
{
    EqSettings s;
    auto v = [this] (const char* id) { return (double) proc.getAPVTS().getRawParameterValue (id)->load(); };
    s.enabled = v (pid::eqOn) > 0.5;
    s.postCab = v (pid::eqPost) > 0.5;
    s.outputDb = v (pid::eqOut);
    for (int b = 0; b < 7; ++b)
    {
        auto& bd = s.band[(size_t) b];
        bd.on = v (pid::eqBand[b][0]) > 0.5;
        bd.freq = v (pid::eqBand[b][1]);
        if (*pid::eqBand[b][2]) bd.gainDb = v (pid::eqBand[b][2]);
        if (*pid::eqBand[b][3]) bd.q = v (pid::eqBand[b][3]);
        if (*pid::eqBand[b][4]) bd.slope = (int) v (pid::eqBand[b][4]);
    }
    return s;
}

float EqPanel::freqToX (double f) const
{
    const auto r = curveArea.toFloat();
    return r.getX() + r.getWidth() * (float) (std::log (f / kMinF) / std::log (kMaxF / kMinF));
}
double EqPanel::xToFreq (float x) const
{
    const auto r = curveArea.toFloat();
    return kMinF * std::pow (kMaxF / kMinF, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth())));
}
float EqPanel::dbToY (double db) const
{
    const auto r = curveArea.toFloat();
    return r.getCentreY() - (float) (db / kRangeDb) * r.getHeight() * 0.5f;
}
double EqPanel::yToDb (float y) const
{
    const auto r = curveArea.toFloat();
    return juce::jlimit (-kRangeDb, kRangeDb, (double) ((r.getCentreY() - y) / (r.getHeight() * 0.5f)) * kRangeDb);
}

juce::Point<float> EqPanel::nodePos (int b, const EqSettings& s) const
{
    const auto& bd = s.band[(size_t) b];
    return { freqToX (bd.freq), isCut (b) ? dbToY (0.0) : dbToY (bd.gainDb) };
}

int EqPanel::hitNode (juce::Point<float> p) const
{
    const auto s = currentSettings();
    int best = -1;
    float bestD = 14.0f;
    for (int b = 0; b < 7; ++b)
    {
        const float d = nodePos (b, s).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqPanel::setParam (const char* id, float value)
{
    if (auto* prm = proc.getAPVTS().getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

void EqPanel::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitNode (e.position);
    if (dragging >= 0)
    {
        for (int k = 1; k <= 3; ++k)
            if (*pid::eqBand[dragging][k]) proc.getAPVTS().getParameter (pid::eqBand[dragging][k])->beginChangeGesture();
        if (! currentSettings().band[(size_t) dragging].on) setParam (pid::eqBand[dragging][0], 1.0f); // dragging wakes a band
    }
}

void EqPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    setParam (pid::eqBand[dragging][1], (float) xToFreq (e.position.x));
    if (*pid::eqBand[dragging][2]) setParam (pid::eqBand[dragging][2], (float) yToDb (e.position.y));
    repaint();
}

void EqPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (int k = 1; k <= 3; ++k)
            if (*pid::eqBand[dragging][k]) proc.getAPVTS().getParameter (pid::eqBand[dragging][k])->endChangeGesture();
    dragging = -1;
}

void EqPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitNode (e.position);
    if (b < 0) return;
    // double-click a node: flatten it (cuts toggle off)
    if (isCut (b)) setParam (pid::eqBand[b][0], currentSettings().band[(size_t) b].on ? 0.0f : 1.0f);
    else setParam (pid::eqBand[b][2], 0.0f);
}

void EqPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = hitNode (e.position);
    if (b < 0) return;
    const auto s = currentSettings().band[(size_t) b];
    if (*pid::eqBand[b][3]) setParam (pid::eqBand[b][3], (float) juce::jlimit (0.1, 10.0, s.q * std::pow (1.25, w.deltaY * 4.0)));
    else if (*pid::eqBand[b][4]) setParam (pid::eqBand[b][4], (float) juce::jlimit (0, 2, s.slope + (w.deltaY > 0 ? 1 : -1)));
}

void EqPanel::timerCallback()
{
    // spectrum: newest samples into a ring, then a windowed FFT, smoothed in dB
    const int got = proc.getChain().readSpectrumSamples (incoming.data(), (int) incoming.size());
    for (int i = 0; i < got; ++i) { ring[(size_t) ringPos] = incoming[(size_t) i]; ringPos = (ringPos + 1) % kFftSize; }
    for (int i = 0; i < kFftSize; ++i) fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % kFftSize)];
    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    for (int i = 0; i < kFftSize / 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * 4.0f / kFftSize, -100.0f);
        auto& s = spectrumDb[(size_t) i];
        s = db > s ? 0.5f * s + 0.5f * db : 0.85f * s + 0.15f * db; // fast rise, slow fall
    }
    hover = hitNode (getMouseXYRelative().toFloat());
    repaint (curveArea.expanded (4));
}

void EqPanel::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    const auto s = currentSettings();
    const double fs = proc.getChain().getSampleRate();

    ui::drawPanel (g, getLocalBounds().reduced (24, 10).toFloat(), "Studio EQ");
    auto r = curveArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 6.0f);

    // grid
    g.setFont (ui::body (10.5f));
    for (double f : { 30.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = freqToX (f);
        g.setColour (ui::line.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (ui::textDim);
        g.drawText (f >= 1000 ? juce::String (f / 1000.0, 0) + "k" : juce::String ((int) f), (int) x - 20, (int) r.getBottom() + 2, 40, 14,
                    juce::Justification::centred);
    }
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        const float y = dbToY (db);
        g.setColour (db == 0.0 ? ui::line.brighter (0.5f) : ui::line.withAlpha (0.7f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (ui::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), (int) r.getX() + 4, (int) y - 13, 30, 12, juce::Justification::left);
    }

    // spectrum (analyser of the output)
    {
        juce::Path sp;
        bool started = false;
        for (int px = 0; px < (int) r.getWidth(); px += 2)
        {
            // each pixel column shows the loudest bin in its frequency span (smooth at the low end too)
            const double f0 = xToFreq (r.getX() + (float) px), f1 = xToFreq (r.getX() + (float) px + 2.0f);
            const double b0 = f0 / fs * kFftSize, b1 = f1 / fs * kFftSize;
            float db = -100.0f;
            if (b1 - b0 < 1.0)
            {
                const int i0 = juce::jlimit (1, kFftSize / 2 - 2, (int) b0);
                const float t = (float) (b0 - i0);
                db = spectrumDb[(size_t) i0] + t * (spectrumDb[(size_t) i0 + 1] - spectrumDb[(size_t) i0]);
            }
            else
                for (int i = juce::jlimit (1, kFftSize / 2 - 1, (int) b0); i <= juce::jlimit (1, kFftSize / 2 - 1, (int) b1); ++i)
                    db = std::max (db, spectrumDb[(size_t) i]);
            const float y = juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, r.getBottom(), r.getY() + r.getHeight() * 0.1f);
            if (! started) { sp.startNewSubPath (r.getX() + (float) px, y); started = true; }
            else sp.lineTo (r.getX() + (float) px, y);
        }
        juce::Path fill = sp;
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        g.setColour (ui::textDim.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (ui::textDim.withAlpha (0.35f));
        g.strokePath (sp, juce::PathStrokeType (1.0f));
    }

    // per-band curves (faint) and the total response
    juce::Path total;
    for (int px = 0; px <= (int) r.getWidth(); px += 2)
    {
        const double f = xToFreq (r.getX() + (float) px);
        const float y = dbToY (juce::jlimit (-kRangeDb, kRangeDb, ParametricEQ::responseDb (s, fs, f)));
        if (px == 0) total.startNewSubPath (r.getX(), y); else total.lineTo (r.getX() + (float) px, y);
    }
    juce::Path area = total;
    area.lineTo (r.getRight(), dbToY (0.0));
    area.lineTo (r.getX(), dbToY (0.0));
    area.closeSubPath();
    g.setColour (ui::gold.withAlpha (s.enabled ? 0.14f : 0.05f));
    g.fillPath (area);
    g.setColour (s.enabled ? ui::gold : ui::textDim);
    g.strokePath (total, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // nodes
    for (int b = 0; b < 7; ++b)
    {
        const auto& bd = s.band[(size_t) b];
        const auto pnt = nodePos (b, s);
        const float rad = (b == dragging || b == hover) ? 9.0f : 7.0f;
        g.setColour (bandColours[b].withAlpha (bd.on ? 1.0f : 0.3f));
        g.fillEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (pnt));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (pnt), 1.2f);
        g.setColour (ui::ink);
        g.setFont (ui::semi (9.0f));
        const char* tag[] = { "LC", "LS", "1", "2", "3", "HS", "HC" };
        g.drawText (tag[b], juce::Rectangle<float> (20, 12).withCentre (pnt), juce::Justification::centred);
        if (b == dragging || b == hover)
        {
            juce::String info = juce::String (pid::eqBandName[b]) + "  " + (bd.freq >= 1000 ? juce::String (bd.freq / 1000.0, 2) + " kHz" : juce::String ((int) bd.freq) + " Hz");
            if (! isCut (b)) info << "  " << (bd.gainDb > 0 ? "+" : "") << juce::String (bd.gainDb, 1) << " dB  Q " << juce::String (bd.q, 2);
            g.setColour (ui::text);
            g.setFont (ui::semi (12.0f));
            g.drawText (info, juce::Rectangle<float> (260, 18).withCentre (pnt.translated (0, -22)), juce::Justification::centred);
        }
    }

    // band strip backgrounds
    const int w = controlsArea.getWidth() / 8;
    for (int b = 0; b < 7; ++b)
    {
        auto col = juce::Rectangle<int> (controlsArea.getX() + b * w, controlsArea.getY(), w, controlsArea.getHeight()).reduced (4, 0);
        g.setColour (ui::panel);
        g.fillRoundedRectangle (col.toFloat(), 6.0f);
        g.setColour (bandColours[b]);
        g.fillRoundedRectangle (col.toFloat().removeFromTop (3.0f), 1.5f);
    }
    auto outCol = juce::Rectangle<int> (controlsArea.getX() + 7 * w, controlsArea.getY(), controlsArea.getRight() - (controlsArea.getX() + 7 * w), controlsArea.getHeight()).reduced (4, 0);
    g.setColour (ui::panel);
    g.fillRoundedRectangle (outCol.toFloat(), 6.0f);
}

void EqPanel::resized()
{
    auto r = getLocalBounds().reduced (38, 0);
    r.removeFromTop (48);
    auto top = getLocalBounds().reduced (38, 0).withHeight (30).withY (12);
    eqOn.setBounds (top.removeFromRight (110));
    top.removeFromRight (10);
    position.setBounds (top.removeFromRight (140).reduced (0, 2));
    curveArea = r.removeFromTop (262);
    r.removeFromTop (34);
    controlsArea = r.withTrimmedBottom (14);

    const int w = controlsArea.getWidth() / 8;
    for (int b = 0; b < 7; ++b)
    {
        auto col = juce::Rectangle<int> (controlsArea.getX() + b * w, controlsArea.getY(), w, controlsArea.getHeight()).reduced (8, 8);
        auto& ui = bands[(size_t) b];
        ui.on->setBounds (col.removeFromTop (26));
        if (ui.slope) { col.removeFromTop (4); ui.slope->setBounds (col.removeFromTop (26)); }
        col.removeFromTop (4);
        if (ui.gain)
        {
            ui.freq->setBounds (col.removeFromTop (col.getHeight() / 2));
            auto row = col;
            ui.gain->setBounds (row.removeFromLeft (row.getWidth() / 2));
            if (ui.q) ui.q->setBounds (row);
        }
        else
            ui.freq->setBounds (col.removeFromTop (std::min (col.getHeight(), 120)));
    }
    auto outCol = juce::Rectangle<int> (controlsArea.getX() + 7 * w, controlsArea.getY(), controlsArea.getRight() - (controlsArea.getX() + 7 * w), controlsArea.getHeight()).reduced (8, 8);
    output.setBounds (outCol.removeFromTop (110));
}
} // namespace namvx
