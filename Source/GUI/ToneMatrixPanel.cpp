#include "ToneMatrixPanel.h"

namespace namvx
{
namespace
{
const char* rowNames[] = { "Gain", "Resonance", "Bass", "Middle", "Treble", "Presence" };
const char* rowHelp[] = { "distortion, compression & feel", "60 - 120 Hz", "120 - 300 Hz", "300 Hz - 1.5 kHz",
                          "1.5 - 4 kHz", "4 - 8 kHz" };
} // namespace

juce::String ToneMatrixPanel::paramId (int row, int model) const
{
    return row == 0 ? juce::String (pid::blendMix[model]) : juce::String (pid::toneSource[row - 1][model]);
}

ToneMatrixPanel::ToneMatrixPanel (NamVXProcessor& p) : proc (p)
{
    for (int r = 0; r < kRows; ++r)
    {
        for (int m = 0; m < kModels; ++m)
        {
            auto& s = cells[r][m];
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
            s.setColour (juce::Slider::textBoxTextColourId, ui::textDim);
            s.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
            attach[r][m] = std::make_unique<APVTS::SliderAttachment> (p.getAPVTS(), paramId (r, m), s);
            addChildComponent (s);

            auto& b = solo[r][m];
            b.setButtonText ("only");
            b.setTooltip ("Take this control entirely from this capture");
            b.onClick = [this, r, m] { setRow (r, m); };
            addChildComponent (b);
        }
        even[r].setButtonText ("even");
        even[r].onClick = [this, r] { setRow (r, -1); };
        addChildComponent (even[r]);
        readout[r].setFont (ui::semi (12.5f));
        readout[r].setJustificationType (juce::Justification::centredRight);
        addChildComponent (readout[r]);
    }
    for (auto& l : modelNames)
    {
        l.setFont (ui::semi (13.0f));
        l.setColour (juce::Label::textColourId, ui::text);
        l.setJustificationType (juce::Justification::centred);
        addChildComponent (l);
    }
    resetAll.onClick = [this] { for (int r = 0; r < kRows; ++r) setRow (r, -1); };
    addChildComponent (resetAll);
    refresh();
    startTimerHz (10);
}

void ToneMatrixPanel::setRow (int row, int soloModel)
{
    for (int m = 0; m < kModels; ++m)
        if (auto* prm = proc.getAPVTS().getParameter (paramId (row, m)))
            prm->setValueNotifyingHost (soloModel < 0 || soloModel == m ? 1.0f : 0.0f);
}

void ToneMatrixPanel::refresh()
{
    const auto* amp = proc.getCurrentAmp();
    isBlend = amp != nullptr && amp->isBlend && ! amp->channels.empty();
    numModels = 0;
    if (isBlend)
        for (auto& cap : amp->channels[0].captures)
        {
            if (! cap.enabled || numModels >= kModels) continue;
            modelNames[numModels].setText (cap.meta.name.empty() ? cap.meta.fileName : cap.meta.name, juce::dontSendNotification);
            ++numModels;
        }
    for (int m = 0; m < kModels; ++m) modelNames[m].setVisible (isBlend && m < numModels);
    for (int r = 0; r < kRows; ++r)
    {
        for (int m = 0; m < kModels; ++m)
        {
            cells[r][m].setVisible (isBlend && m < numModels);
            solo[r][m].setVisible (isBlend && m < numModels);
        }
        even[r].setVisible (isBlend);
        readout[r].setVisible (isBlend && r > 0);
    }
    resetAll.setVisible (isBlend);
    resized();
    repaint();
}

void ToneMatrixPanel::timerCallback()
{
    if (! isBlend) return;
    for (int r = 1; r < kRows; ++r)
    {
        const float db = proc.getChain().toneCorrectionDb[(size_t) (r - 1)].load();
        const auto txt = std::abs (db) < 0.05f ? juce::String ("0.0 dB") : (db > 0 ? "+" : "") + juce::String (db, 1) + " dB";
        readout[r].setColour (juce::Label::textColourId, std::abs (db) < 0.05f ? ui::textDim : ui::amber);
        if (readout[r].getText() != txt) readout[r].setText (txt, juce::dontSendNotification);
    }
}

void ToneMatrixPanel::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    ui::drawPanel (g, area.toFloat(), "Tone matrix  -  build one amp from all your NAM captures");
    auto r = area.reduced (16, 12);
    r.removeFromTop (24);
    g.setFont (ui::body (12.5f));
    g.setColour (ui::textDim);
    if (! isBlend)
    {
        g.drawFittedText ("The tone matrix works on model blends. Go to Library > \"New blend from NAM models...\", pick 2-4 captures "
                          "(any amps), and every control here can then take its character from any of them: for example the gain "
                          "of capture A, the bass of capture B and the mids of capture C.",
                          r.reduced (0, 20).toNearestInt(), juce::Justification::centredTop, 4);
        return;
    }
    g.drawFittedText ("Gain picks which captures make the sound (distortion, compression, feel). Each EQ row gives that band the "
                      "tonal balance of the captures you choose, transferred onto the gain sound as one phase-coherent signal. "
                      "The amp knobs still work on top.", r.removeFromTop (34).toNearestInt(), juce::Justification::topLeft, 2);

    // row labels + separators
    const int rowH = grid.getHeight() / (kRows + 1);
    for (int row = 0; row < kRows; ++row)
    {
        auto lr = juce::Rectangle<int> (grid.getX(), grid.getY() + rowH * (row + 1), 170, rowH);
        g.setColour (row == 0 ? ui::gold : ui::text);
        g.setFont (ui::semi (14.0f));
        g.drawText (juce::String (rowNames[row]).toUpperCase(), lr.removeFromTop (rowH / 2 + 4), juce::Justification::bottomLeft);
        g.setColour (ui::textDim);
        g.setFont (ui::body (11.5f));
        g.drawText (rowHelp[row], lr, juce::Justification::topLeft);
        g.setColour (ui::line);
        g.drawHorizontalLine (grid.getY() + rowH * (row + 1), (float) grid.getX(), (float) grid.getRight());
    }
    g.setColour (ui::textDim);
    g.setFont (ui::semi (10.5f).withExtraKerningFactor (0.15f));
    g.drawText ("APPLIED", juce::Rectangle<int> (grid.getRight() - 80, grid.getY(), 80, rowH), juce::Justification::centredRight);
}

void ToneMatrixPanel::resized()
{
    area = getLocalBounds().reduced (24, 18);
    auto r = area.reduced (16, 12);
    r.removeFromTop (24 + 34 + 10);
    resetAll.setBounds (r.removeFromBottom (34).removeFromRight (190));
    r.removeFromBottom (8);
    grid = r;
    if (! isBlend || numModels == 0) return;

    const int rowH = grid.getHeight() / (kRows + 1);
    const int labelW = 170, readW = 80, evenW = 58;
    const int colW = (grid.getWidth() - labelW - readW - evenW - 12) / numModels;
    for (int m = 0; m < numModels; ++m)
        modelNames[m].setBounds (grid.getX() + labelW + m * colW, grid.getY(), colW, rowH);
    for (int row = 0; row < kRows; ++row)
    {
        const int y = grid.getY() + rowH * (row + 1);
        for (int m = 0; m < numModels; ++m)
        {
            auto cell = juce::Rectangle<int> (grid.getX() + labelW + m * colW, y, colW, rowH).reduced (8, rowH / 2 - 14);
            solo[row][m].setBounds (cell.removeFromRight (46).reduced (0, 3));
            cells[row][m].setBounds (cell);
        }
        even[row].setBounds (grid.getX() + labelW + numModels * colW + 6, y + rowH / 2 - 12, evenW, 24);
        readout[row].setBounds (grid.getRight() - readW, y, readW, rowH);
    }
}
} // namespace namvx
