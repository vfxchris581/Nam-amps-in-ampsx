#include "ToneMatrixPanel.h"

namespace namknobs
{
namespace
{
const char* rowNames[] = { "Gain", "Resonance", "Bass", "Middle", "Treble", "Presence" };
} // namespace

/** Compact draggable matrix cell: a filled bar with a small percentage. */
struct ToneMatrixPanel::MatrixCell : juce::Slider
{
    MatrixCell()
    {
        setSliderStyle (juce::Slider::LinearHorizontal);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRange (0.0, 1.0);
        setDoubleClickReturnValue (true, 1.0);
        setSliderSnapsToMousePosition (false);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (ui::panelHi);
        g.fillRoundedRectangle (r, 3.0f);
        const float v = (float) getValue();
        g.setColour (ui::amber.withAlpha (isEnabled() ? 0.85f : 0.55f));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * v), 3.0f);
        g.setColour (v > 0.55f ? ui::ink : ui::text.withAlpha (0.85f));
        g.setFont (ui::semi (10.0f));
        g.drawText (juce::String (juce::roundToInt (v * 100)), r, juce::Justification::centred);
    }
};

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

ToneMatrixPanel::ToneMatrixPanel (NamKnobsProcessor& p)
    : proc (p), phase (p.getAPVTS(), pid::phaseMode), levelMatch (p.getAPVTS(), pid::levelMatch, "Level match"),
      autoToggle (p.getAPVTS(), pid::matrixAuto, "Auto (knobs blend the captures)")
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
            cells[r][s] = std::make_unique<MatrixCell>();
            auto& c = *cells[r][s];
            c.setTooltip ("Drag to set; right-click for 'only this slot'");
            attach[r][s] = std::make_unique<APVTS::SliderAttachment> (p.getAPVTS(), paramId (r, s), c);
            mouse[r][s] = std::make_unique<CellMouse> (*this, r, s);
            c.addMouseListener (mouse[r][s].get(), true);
            addAndMakeVisible (c);
        }
        if (r > 0)
        {
            auto& t = trim[r];
            t.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            t.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            t.setRotaryParameters (juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true);
            t.setPopupDisplayEnabled (true, true, this);
            t.setDoubleClickReturnValue (true, 0.0);
            t.setTooltip ("Fine-tune " + juce::String (rowNames[r]).toLowerCase() + " (+/-6 dB). Double-click for 0.");
            trimAttach[r] = std::make_unique<APVTS::SliderAttachment> (p.getAPVTS(), pid::toneTrim[r - 1], t);
            addAndMakeVisible (t);
        }
        readout[r].setFont (ui::semi (11.5f));
        readout[r].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (readout[r]);
    }
    for (int s = 0; s < kSlots; ++s)
    {
        names[s].setFont (ui::semi (10.5f));
        names[s].setJustificationType (juce::Justification::centred);
        names[s].setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (names[s]);
        matchLabel[s].setFont (ui::semi (9.5f));
        matchLabel[s].setJustificationType (juce::Justification::centredRight);
        matchLabel[s].setColour (juce::Label::textColourId, ui::green.withAlpha (0.85f));
        matchLabel[s].setTooltip ("Level match (dB): the gain that brings this capture to the same loudness as the others");
        addChildComponent (matchLabel[s]);
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
        invert[s] = std::make_unique<Toggle> (p.getAPVTS(), pid::slotInvert[s], juce::String::fromUTF8 ("\xc3\x98"));
        invert[s]->getProperties().set ("ledRed", true);
        invert[s]->setTooltip ("Flip this capture's polarity");
        addAndMakeVisible (*invert[s]);
    }
    addAndMakeVisible (autoToggle);
    addAndMakeVisible (phase);
    phase.setTooltip ("How blended captures are lined up. Phase-matched measures every capture against the others "
                      "and fixes timing (to a fraction of a sample) and polarity, so blends don't cancel.");
    phaseLabel.setFont (ui::semi (12.0f));
    phaseLabel.setColour (juce::Label::textColourId, ui::textDim);
    phaseLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (phaseLabel);
    resetAll.onClick = [this] { for (int r = 0; r < kRows; ++r) setRow (r, -1); };
    deleteAll.setTooltip ("Remove every NAM capture from the loaders");
    deleteAll.setColour (juce::TextButton::textColourOffId, ui::red.brighter (0.4f));
    deleteAll.onClick = [this] {
        if (numLoaded == 0) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete all captures",
            "Remove all " + juce::String (numLoaded) + " NAM captures from the loaders?\n\nYour original .nam files are not touched.",
            "Delete all", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r) proc.clearAllBlendSlots(); }));
    };
    addAndMakeVisible (deleteAll);
    levelMatch.setTooltip ("Plays every capture at the same loudness (measured the way your ear hears it), "
                           "so clean and high-gain captures sit at one level and blends don't jump in volume.");
    addAndMakeVisible (levelMatch);
    addAndMakeVisible (resetAll);
    refresh();
    startTimerHz (15);
}

ToneMatrixPanel::~ToneMatrixPanel()
{
    for (int r = 0; r < kRows; ++r)
        for (int s = 0; s < kSlots; ++s) cells[r][s]->removeMouseListener (mouse[r][s].get());
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
        names[s].setText (juce::String (s + 1) + ". " + (loaded[s] ? n : juce::String ("empty")), juce::dontSendNotification);
        names[s].setColour (juce::Label::textColourId, loaded[s] ? ui::text : ui::textDim);
        load[s].setButtonText (loaded[s] ? "Replace" : "Load");
        clear[s].setVisible (loaded[s]);
        invert[s]->setVisible (loaded[s]);
        for (int r = 0; r < kRows; ++r) cells[r][s]->setAlpha (loaded[s] ? 1.0f : 0.2f);
    }
    deleteAll.setEnabled (numLoaded > 0);
    applyAutoState();
    resized();
    repaint();
}

void ToneMatrixPanel::applyAutoState()
{
    autoMode = proc.getAPVTS().getRawParameterValue (pid::matrixAuto)->load() > 0.5f;
    for (int r = 0; r < kRows; ++r)
        for (int s = 0; s < kSlots; ++s) cells[r][s]->setEnabled (! autoMode && loaded[s]);
    resetAll.setEnabled (! autoMode);
    if (! autoMode && lastAuto) // back to manual: show each cell's own parameter again
        for (int r = 0; r < kRows; ++r)
            for (int s = 0; s < kSlots; ++s)
                attach[r][s] = std::make_unique<APVTS::SliderAttachment> (proc.getAPVTS(), paramId (r, s), *cells[r][s]);
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
                if (loaded[s]) { cells[r][s]->setValue (ch.matrixWeights[(size_t) r][(size_t) s].load(), juce::dontSendNotification); cells[r][s]->repaint(); }
    const bool lm = proc.getAPVTS().getRawParameterValue (pid::levelMatch)->load() > 0.5f;
    for (int s = 0; s < kSlots; ++s)
    {
        const float v = ch.slotMatchDb[(size_t) s].load();
        const bool show = loaded[s] && std::isfinite (v);
        const auto txt = show ? (v >= 0.0f ? "+" : "") + juce::String (v, 1) : juce::String();
        if (matchLabel[s].getText() != txt) matchLabel[s].setText (txt, juce::dontSendNotification);
        matchLabel[s].setVisible (show);
        matchLabel[s].setAlpha (lm ? 1.0f : 0.35f);
    }
    for (int r = 1; r < kRows; ++r)
    {
        const float db = ch.toneCorrectionDb[(size_t) (r - 1)].load();
        const auto txt = std::abs (db) < 0.05f ? juce::String ("0.0 dB") : (db > 0 ? "+" : "") + juce::String (db, 1) + " dB";
        readout[r].setColour (juce::Label::textColourId, std::abs (db) < 0.05f ? ui::textDim : ui::amber);
        if (readout[r].getText() != txt) readout[r].setText (txt, juce::dontSendNotification);
    }
}

void ToneMatrixPanel::paint (juce::Graphics& g)
{
    ui::drawTolex (g, getLocalBounds().toFloat());

    // loader rack
    ui::drawPanel (g, slotsArea.toFloat(), "NAM loaders  -  " + juce::String (numLoaded) + " of " + juce::String (kSlots));
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
        auto lr = juce::Rectangle<int> (grid.getX(), grid.getY() + r * rowH, nameW, rowH);
        g.setColour (r == 0 ? ui::gold : ui::text);
        g.setFont (ui::semi (12.5f));
        g.drawText (juce::String (rowNames[r]).toUpperCase(), lr, juce::Justification::centredLeft);
    }
}

void ToneMatrixPanel::resized()
{
    auto r = getLocalBounds().reduced (24, 12);
    slotsArea = r.removeFromTop (128);
    r.removeFromTop (12);
    plate = r.removeFromTop (196);
    r.removeFromTop (12);
    matrixArea = r;

    // loaders: two rows of nine
    {
        auto a = slotsArea.reduced (14, 8);
        auto head = a.removeFromTop (22);
        phase.setBounds (head.removeFromRight (150).reduced (0, 0));
        phaseLabel.setBounds (head.removeFromRight (60));
        head.removeFromRight (16);
        deleteAll.setBounds (head.removeFromRight (92));
        head.removeFromRight (16);
        levelMatch.setBounds (head.removeFromRight (124));
        a.removeFromTop (2);
        const int perRow = (kSlots + 1) / 2;
        const int w = a.getWidth() / perRow, h = a.getHeight() / 2;
        for (int s = 0; s < kSlots; ++s)
        {
            const int row = s / perRow, colIdx = s % perRow;
            auto col = juce::Rectangle<int> (a.getX() + colIdx * w, a.getY() + row * h, w, h).reduced (3, 2);
            auto nameRow = col.removeFromTop (18);
            matchLabel[s].setBounds (nameRow.removeFromRight (36));
            names[s].setBounds (nameRow);
            auto b = col.removeFromTop (24);
            clear[s].setBounds (b.removeFromRight (22));
            b.removeFromRight (2);
            invert[s]->setBounds (b.removeFromRight (34));
            b.removeFromRight (2);
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
                cells[row][s]->setBounds (juce::Rectangle<int> (grid.getX() + labelW + s * colW, grid.getY() + row * rowH, colW, rowH)
                                             .reduced (4, 3));
            readout[row].setBounds (grid.getRight() - readW, grid.getY() + row * rowH, readW, rowH);
            if (row > 0)
                trim[row].setBounds (juce::Rectangle<int> (grid.getX() + nameW, grid.getY() + row * rowH, labelW - nameW - 6, rowH)
                                         .withSizeKeepingCentre (std::min (34, rowH), std::min (34, rowH)));
        }
    }
}
} // namespace namknobs
