#include "ToneMatrixPanel.h"

namespace namvx
{
namespace
{
const char* rowNames[] = { "Gain", "Resonance", "Bass", "Middle", "Treble", "Presence" };
} // namespace

struct ToneMatrixPanel::CellMouse : juce::MouseListener
{
    CellMouse (ToneMatrixPanel& o, int r, int s) : owner (o), row (r), slot (s) {}
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) owner.showCellMenu (row, slot); }
    ToneMatrixPanel& owner;
    int row, slot;
};

juce::String ToneMatrixPanel::paramId (int row, int slot) const
{
    return row == 0 ? juce::String (pid::blendMix[slot]) : juce::String (pid::toneSource[row - 1][slot]);
}

ToneMatrixPanel::ToneMatrixPanel (NamVXProcessor& p)
    : proc (p), autoToggle (p.getAPVTS(), pid::matrixAuto, "Auto (knobs blend the captures)")
{
    for (int k = 0; k < kNumControls; ++k)
    {
        ampKnobs.push_back (std::make_unique<Knob> (p.getAPVTS(), controlId ((Control) k), controlName ((Control) k), true));
        addAndMakeVisible (*ampKnobs.back());
    }
    for (int r = 0; r < kRows; ++r)
    {
        for (int s = 0; s < kSlots; ++s)
        {
            auto& c = cells[r][s];
            c.setSliderStyle (juce::Slider::LinearBar);
            c.setColour (juce::Slider::trackColourId, ui::amber.withAlpha (0.75f));
            c.setColour (juce::Slider::backgroundColourId, ui::panelHi);
            c.setColour (juce::Slider::textBoxTextColourId, ui::text);
            c.setColour (juce::Slider::textBoxOutlineColourId, ui::line);
            c.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
            c.setDoubleClickReturnValue (true, 1.0);
            attach[r][s] = std::make_unique<APVTS::SliderAttachment> (p.getAPVTS(), paramId (r, s), c);
            mouse[r][s] = std::make_unique<CellMouse> (*this, r, s);
            c.addMouseListener (mouse[r][s].get(), true);
            addAndMakeVisible (c);
        }
        readout[r].setFont (ui::semi (11.5f));
        readout[r].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (readout[r]);
    }
    for (int s = 0; s < kSlots; ++s)
    {
        names[s].setFont (ui::semi (11.0f));
        names[s].setJustificationType (juce::Justification::centred);
        names[s].setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (names[s]);
        load[s].onClick = [this, s] {
            chooser = std::make_unique<juce::FileChooser> ("Load a NAM capture into slot " + juce::String (s + 1), juce::File(), "*.nam;*.NAM");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, s] (const juce::FileChooser& fc) {
                                      if (fc.getResult().existsAsFile()) proc.loadBlendSlot (s, fc.getResult());
                                  });
        };
        addAndMakeVisible (load[s]);
        clear[s].setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        clear[s].setTooltip ("Empty this slot");
        clear[s].onClick = [this, s] { proc.clearBlendSlot (s); };
        addAndMakeVisible (clear[s]);
    }
    addAndMakeVisible (autoToggle);
    resetAll.onClick = [this] { for (int r = 0; r < kRows; ++r) setRow (r, -1); };
    addAndMakeVisible (resetAll);
    refresh();
    startTimerHz (15);
}

ToneMatrixPanel::~ToneMatrixPanel()
{
    for (int r = 0; r < kRows; ++r)
        for (int s = 0; s < kSlots; ++s) cells[r][s].removeMouseListener (mouse[r][s].get());
}

void ToneMatrixPanel::setRow (int row, int soloSlot)
{
    for (int s = 0; s < kSlots; ++s)
        if (auto* prm = proc.getAPVTS().getParameter (paramId (row, s)))
            prm->setValueNotifyingHost (soloSlot < 0 ? 1.0f : (s == soloSlot ? 1.0f : 0.0f));
}

void ToneMatrixPanel::showCellMenu (int row, int slot)
{
    juce::PopupMenu m;
    m.addSectionHeader (juce::String (rowNames[row]) + " - slot " + juce::String (slot + 1));
    m.addItem (1, "Take " + juce::String (rowNames[row]).toLowerCase() + " only from this slot", loaded[slot] && ! autoMode);
    m.addItem (2, "Even blend for this row", ! autoMode);
    m.addItem (3, "Use this slot for everything", loaded[slot] && ! autoMode);
    if (autoMode) m.addItem (4, "Turn Auto off to set the blend by hand");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, row, slot] (int r) {
        if (r == 1) setRow (row, slot);
        if (r == 2) setRow (row, -1);
        if (r == 3) for (int i = 0; i < kRows; ++i) setRow (i, slot);
        if (r == 4) if (auto* prm = proc.getAPVTS().getParameter (pid::matrixAuto)) prm->setValueNotifyingHost (0.0f);
    });
}

void ToneMatrixPanel::refresh()
{
    const auto* amp = proc.getCurrentAmp();
    const bool blend = amp != nullptr && amp->isBlend && ! amp->channels.empty();
    numLoaded = 0;
    for (int s = 0; s < kSlots; ++s)
    {
        loaded[s] = false;
        juce::String n;
        if (blend)
        {
            int idx = 0;
            for (auto& cap : amp->channels[0].captures)
            {
                if ((cap.blendSlot >= 0 ? cap.blendSlot : idx) == s)
                {
                    loaded[s] = true;
                    n = cap.meta.name.empty() ? cap.meta.fileName : cap.meta.name;
                }
                ++idx;
            }
        }
        if (loaded[s]) ++numLoaded;
        names[s].setText (loaded[s] ? n : juce::String ("Slot ") + juce::String (s + 1), juce::dontSendNotification);
        names[s].setColour (juce::Label::textColourId, loaded[s] ? ui::text : ui::textDim);
        load[s].setButtonText (loaded[s] ? "Replace" : "Load NAM");
        clear[s].setVisible (loaded[s]);
        for (int r = 0; r < kRows; ++r) cells[r][s].setAlpha (loaded[s] ? 1.0f : 0.2f);
    }
    applyAutoState();
    resized();
    repaint();
}

void ToneMatrixPanel::applyAutoState()
{
    autoMode = proc.getAPVTS().getRawParameterValue (pid::matrixAuto)->load() > 0.5f;
    for (int r = 0; r < kRows; ++r)
        for (int s = 0; s < kSlots; ++s) cells[r][s].setEnabled (! autoMode && loaded[s]);
    resetAll.setEnabled (! autoMode);
    if (! autoMode && lastAuto) // back to manual: show each cell's own parameter again
        for (int r = 0; r < kRows; ++r)
            for (int s = 0; s < kSlots; ++s)
                attach[r][s] = std::make_unique<APVTS::SliderAttachment> (proc.getAPVTS(), paramId (r, s), cells[r][s]);
    lastAuto = autoMode;
}

void ToneMatrixPanel::timerCallback()
{
    if ((proc.getAPVTS().getRawParameterValue (pid::matrixAuto)->load() > 0.5f) != autoMode)
    {
        applyAutoState();
        repaint();
    }
    auto& ch = proc.getChain();
    if (autoMode && numLoaded > 0)
        for (int r = 0; r < kRows; ++r)
            for (int s = 0; s < kSlots; ++s)
                if (loaded[s]) cells[r][s].setValue (ch.matrixWeights[(size_t) r][(size_t) s].load(), juce::dontSendNotification);
    for (int r = 1; r < kRows; ++r)
    {
        const float db = numLoaded > 0 ? ch.toneCorrectionDb[(size_t) (r - 1)].load() : 0.0f;
        const auto txt = std::abs (db) < 0.05f ? juce::String ("0.0 dB") : (db > 0 ? "+" : "") + juce::String (db, 1) + " dB";
        readout[r].setColour (juce::Label::textColourId, std::abs (db) < 0.05f ? ui::textDim : ui::amber);
        if (readout[r].getText() != txt) readout[r].setText (txt, juce::dontSendNotification);
    }
}

void ToneMatrixPanel::paint (juce::Graphics& g)
{
    ui::drawTolex (g, getLocalBounds().toFloat());

    // loader rack
    ui::drawPanel (g, slotsArea.toFloat(), "NAM loaders");
    // amp faceplate
    ui::drawFaceplate (g, plate.toFloat());
    g.setColour (ui::ink.withAlpha (0.65f));
    g.setFont (ui::semi (11.0f).withExtraKerningFactor (0.22f));
    g.drawText (numLoaded > 0 ? juce::String (numLoaded) + (numLoaded == 1 ? " CAPTURE" : " CAPTURES") + " PLAYING AS ONE AMP"
                              : juce::String ("LOAD CAPTURES ABOVE - THEY BECOME ONE AMP"),
                plate.reduced (40, 0).removeFromTop (30).translated (0, 6), juce::Justification::centred);

    // blend matrix
    ui::drawPanel (g, matrixArea.toFloat(), autoMode ? "Blend matrix - set by the amp knobs" : "Blend matrix - manual");
    const int rowH = grid.getHeight() / kRows;
    for (int r = 0; r < kRows; ++r)
    {
        auto lr = juce::Rectangle<int> (grid.getX(), grid.getY() + r * rowH, labelW, rowH);
        g.setColour (r == 0 ? ui::gold : ui::text);
        g.setFont (ui::semi (12.5f));
        g.drawText (juce::String (rowNames[r]).toUpperCase(), lr, juce::Justification::centredLeft);
    }
}

void ToneMatrixPanel::resized()
{
    auto r = getLocalBounds().reduced (24, 12);
    slotsArea = r.removeFromTop (92);
    r.removeFromTop (12);
    plate = r.removeFromTop (230);
    r.removeFromTop (12);
    matrixArea = r;

    // loaders
    {
        auto a = slotsArea.reduced (14, 10);
        a.removeFromTop (22);
        const int w = a.getWidth() / kSlots;
        for (int s = 0; s < kSlots; ++s)
        {
            auto col = juce::Rectangle<int> (a.getX() + s * w, a.getY(), w, a.getHeight()).reduced (4, 0);
            names[s].setBounds (col.removeFromTop (26));
            auto b = col.removeFromTop (26);
            clear[s].setBounds (b.removeFromRight (26));
            b.removeFromRight (3);
            load[s].setBounds (b);
        }
    }
    // faceplate knobs
    {
        auto row = plate.reduced (28, 0).withTrimmedTop (36).withTrimmedBottom (10);
        const int kw = row.getWidth() / kNumControls;
        for (int k = 0; k < kNumControls; ++k) ampKnobs[(size_t) k]->setBounds (row.removeFromLeft (kw).reduced (6, 0));
    }
    // matrix
    {
        auto a = matrixArea.reduced (14, 10);
        auto head = a.removeFromTop (24);
        resetAll.setBounds (head.removeFromRight (110).withTrimmedBottom (2));
        head.removeFromRight (10);
        autoToggle.setBounds (head.removeFromRight (260));
        a.removeFromTop (4);
        grid = a;
        const int rowH = grid.getHeight() / kRows;
        const int colW = (grid.getWidth() - labelW - readW) / kSlots;
        for (int row = 0; row < kRows; ++row)
        {
            for (int s = 0; s < kSlots; ++s)
                cells[row][s].setBounds (juce::Rectangle<int> (grid.getX() + labelW + s * colW, grid.getY() + row * rowH, colW, rowH)
                                             .reduced (4, 3));
            readout[row].setBounds (grid.getRight() - readW, grid.getY() + row * rowH, readW, rowH);
        }
    }
}
} // namespace namvx
