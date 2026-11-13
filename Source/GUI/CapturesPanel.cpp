#include "CapturesPanel.h"
#include "ChannelManager/ChannelManager.h"

namespace namknobs
{
class CapturesPanel::EditableCell : public juce::Label
{
public:
    EditableCell (CapturesPanel& o) : owner (o)
    {
        setEditable (false, true, false);
        setFont (ui::body (13.0f));
        setJustificationType (juce::Justification::centred);
        setColour (juce::Label::textColourId, ui::text);
        setColour (juce::Label::textWhenEditingColourId, ui::text);
        setColour (juce::Label::backgroundWhenEditingColourId, ui::bg);
        setColour (juce::Label::outlineWhenEditingColourId, ui::gold);
    }
    void textWasEdited() override { owner.cellEdited (row, col, getText()); }
    void set (int r, int c, const juce::String& t, juce::Colour colour)
    {
        row = r; col = c;
        setText (t, juce::dontSendNotification);
        setColour (juce::Label::textColourId, colour);
    }
    int row = 0, col = 0;

private:
    CapturesPanel& owner;
};

CapturesPanel::CapturesPanel (NamKnobsProcessor& p) : proc (p)
{
    auto& h = table.getHeader();
    h.addColumn ("Capture", colName, 230, 120, 600);
    h.addColumn ("Channel", colChannel, 92);
    const char* shortNames[] = { "Gain", "Vol", "Bass", "Mid", "Treb", "Pres", "Res", "Mstr" };
    for (int k = 0; k < kNumControls; ++k) h.addColumn (shortNames[k], colFirstKnob + k, 52);
    h.addColumn ("Trim dB", colTrim, 62);
    h.addColumn ("On", colOn, 38);
    h.addColumn ("Analysis", colInfo, 190, 80, 400);
    h.setStretchToFitActive (true);
    table.setRowHeight (28);
    table.setColour (juce::ListBox::outlineColourId, ui::line);
    table.setOutlineThickness (0);
    addAndMakeVisible (table);

    for (auto* b : { &importButton, &applyButton, &analyseButton, &revertButton }) addAndMakeVisible (b);
    importButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Import NAM captures (files or folders)", juce::File(), "*.nam");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) {
                                  juce::StringArray paths;
                                  for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                  if (! paths.isEmpty()) proc.importFiles (paths);
                              });
    };
    applyButton.onClick = [this] {
        for (auto& ch : working.channels) ChannelManager::refreshEstimates (ch);
        ChannelManager::pruneEmpty (working);
        ChannelManager::sortChannels (working);
        proc.updateAmp (working);
        dirty = false;
        refresh();
    };
    revertButton.onClick = [this] { dirty = false; refresh(); };
    analyseButton.onClick = [this] { proc.reanalyseCurrentAmp(); };

    pendingTitle.setFont (ui::semi (13.0f));
    pendingTitle.setColour (juce::Label::textColourId, ui::amber);
    addAndMakeVisible (pendingTitle);
    for (auto* e : { &makeEd, &modelEd, &channelEd })
    {
        e->setFont (ui::body (13.0f));
        e->setIndents (8, 6);
        addAndMakeVisible (e);
    }
    makeEd.setTextToShowWhenEmpty ("Manufacturer", ui::textDim);
    modelEd.setTextToShowWhenEmpty ("Model", ui::textDim);
    channelEd.setTextToShowWhenEmpty ("Channel (optional)", ui::textDim);
    addAndMakeVisible (assignButton);
    addAndMakeVisible (skipButton);
    assignButton.onClick = [this] {
        proc.assignPending (pendingIndex, makeEd.getText(), modelEd.getText(), channelEd.getText());
        pendingIndex = 0;
        refresh();
    };
    skipButton.onClick = [this] { proc.discardPending (pendingIndex); pendingIndex = 0; refresh(); };
    refresh();
}

void CapturesPanel::refresh()
{
    if (! dirty)
    {
        if (const auto* a = proc.getCurrentAmp()) working = *a;
        else working = {};
    }
    rows.clear();
    for (int c = 0; c < (int) working.channels.size(); ++c)
        for (int i = 0; i < (int) working.channels[(size_t) c].captures.size(); ++i) rows.push_back ({ c, i });
    table.updateContent();
    table.repaint();
    applyButton.setToggleState (dirty, juce::dontSendNotification);
    applyButton.setEnabled (dirty);
    revertButton.setEnabled (dirty);
    showPending();
    resized();
    repaint();
}

void CapturesPanel::showPending()
{
    const auto pend = proc.getPending();
    const bool any = ! pend.empty();
    for (juce::Component* c : { (juce::Component*) &pendingTitle, (juce::Component*) &makeEd, (juce::Component*) &modelEd,
                                (juce::Component*) &channelEd, (juce::Component*) &assignButton, (juce::Component*) &skipButton })
        c->setVisible (any);
    if (! any) return;
    pendingIndex = juce::jlimit (0, (int) pend.size() - 1, pendingIndex);
    const auto& p = pend[(size_t) pendingIndex];
    pendingTitle.setText ("Needs identification (" + juce::String (pendingIndex + 1) + " of " + juce::String ((int) pend.size())
                              + "): " + juce::String (p.meta.name) + "  -  no manufacturer/model in its metadata",
                          juce::dontSendNotification);
    if (makeEd.isEmpty()) makeEd.setText (p.meta.suggestedManufacturer, false);
    if (modelEd.isEmpty()) modelEd.setText (p.meta.suggestedModel, false);
}

void CapturesPanel::markDirty()
{
    dirty = true;
    applyButton.setEnabled (true);
    revertButton.setEnabled (true);
    applyButton.setToggleState (true, juce::dontSendNotification);
}

void CapturesPanel::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    auto r = getLocalBounds().reduced (24, 18).toFloat();
    ui::drawPanel (g, r, working.name.empty() ? juce::String ("NAM captures") : juce::String ("NAM captures  -  " + working.name));
    g.setColour (ui::textDim);
    g.setFont (ui::body (12.0f));
    g.drawText ("Double-click a knob value to map it. White = from the file name, amber ~ = estimated from the sound, gold = set by you, "
                "blank = unknown (that knob is realised by DSP instead). Type a channel name to move a capture.",
                r.reduced (16, 0).withTrimmedTop (30).withHeight (18), juce::Justification::centredLeft);
}

void CapturesPanel::resized()
{
    auto r = getLocalBounds().reduced (24, 18).reduced (16, 0);
    r.removeFromTop (54);
    auto buttons = r.removeFromTop (34);
    importButton.setBounds (buttons.removeFromLeft (170));
    buttons.removeFromLeft (8);
    analyseButton.setBounds (buttons.removeFromLeft (150));
    revertButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    applyButton.setBounds (buttons.removeFromRight (140));
    r.removeFromTop (10);
    if (pendingTitle.isVisible())
    {
        auto pend = r.removeFromBottom (76).withTrimmedBottom (14);
        pendingTitle.setBounds (pend.removeFromTop (24));
        auto f = pend.removeFromTop (32);
        makeEd.setBounds (f.removeFromLeft (180)); f.removeFromLeft (8);
        modelEd.setBounds (f.removeFromLeft (220)); f.removeFromLeft (8);
        channelEd.setBounds (f.removeFromLeft (160)); f.removeFromLeft (8);
        assignButton.setBounds (f.removeFromLeft (170)); f.removeFromLeft (8);
        skipButton.setBounds (f.removeFromLeft (90));
    }
    table.setBounds (r.withTrimmedBottom (16));
}

void CapturesPanel::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    g.setColour (selected ? ui::gold.withAlpha (0.15f) : (row % 2 ? ui::panel : ui::panelHi.withAlpha (0.5f)));
    g.fillRect (0, 0, w, h);
}

juce::String CapturesPanel::cellText (int row, int col) const
{
    const auto& rr = rows[(size_t) row];
    const auto& ch = working.channels[(size_t) rr.channel];
    const auto& c = ch.captures[(size_t) rr.index];
    if (col == colName) return c.meta.name.empty() ? juce::String (c.meta.fileName) : juce::String (c.meta.name);
    if (col == colChannel) return ch.name;
    if (col >= colFirstKnob && col < colFirstKnob + kNumControls)
    {
        const int k = col - colFirstKnob;
        const double v = c.coords[(size_t) k];
        if (! isKnown (v)) return {};
        return (c.source[(size_t) k] == CoordSource::Estimated ? "~" : "") + juce::String (v, v == std::round (v) ? 0 : 1);
    }
    if (col == colTrim) return juce::String (c.trimDb, 1);
    if (col == colInfo)
    {
        if (! c.analysis || ! c.analysis->valid) return "not analysed";
        const auto& a = *c.analysis;
        return juce::String::fromUTF8 ("THD ") + juce::String (juce::roundToInt (a.thdDb)) + " dB  \xc2\xb7  "
               + juce::String (c.meta.architecture) + " " + juce::String (juce::roundToInt (a.sampleRate / 1000.0)) + "k";
    }
    return {};
}

void CapturesPanel::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row >= (int) rows.size()) return;
    if (col == colName || col == colInfo)
    {
        g.setColour (col == colName ? ui::text : ui::textDim);
        g.setFont (col == colName ? ui::semi (13.0f) : ui::body (12.0f));
        g.drawText (cellText (row, col), 10, 0, w - 14, h, juce::Justification::centredLeft, true);
    }
    if (col == colOn)
    {
        const auto& c = working.channels[(size_t) rows[(size_t) row].channel].captures[(size_t) rows[(size_t) row].index];
        ui::drawLed (g, juce::Rectangle<float> (10, 10).withCentre ({ w * 0.5f, h * 0.5f }), ui::amber, c.enabled);
    }
}

juce::Component* CapturesPanel::refreshComponentForCell (int row, int col, bool, juce::Component* existing)
{
    const bool editable = col == colChannel || (col >= colFirstKnob && col < colFirstKnob + kNumControls) || col == colTrim;
    if (! editable || row >= (int) rows.size()) { delete existing; return nullptr; }
    auto* cell = dynamic_cast<EditableCell*> (existing);
    if (cell == nullptr) { delete existing; cell = new EditableCell (*this); }

    juce::Colour colour = ui::text;
    if (col >= colFirstKnob && col < colFirstKnob + kNumControls)
    {
        const auto& c = working.channels[(size_t) rows[(size_t) row].channel].captures[(size_t) rows[(size_t) row].index];
        const auto src = c.source[(size_t) (col - colFirstKnob)];
        colour = src == CoordSource::Estimated ? ui::amber : (src == CoordSource::User ? ui::gold : ui::text);
    }
    cell->set (row, col, cellText (row, col), colour);
    return cell;
}

void CapturesPanel::cellClicked (int row, int col, const juce::MouseEvent&)
{
    if (col != colOn || row >= (int) rows.size()) return;
    auto& c = working.channels[(size_t) rows[(size_t) row].channel].captures[(size_t) rows[(size_t) row].index];
    c.enabled = ! c.enabled;
    markDirty();
    table.repaintRow (row);
}

void CapturesPanel::cellEdited (int row, int col, const juce::String& text)
{
    if (row >= (int) rows.size()) return;
    const auto rr = rows[(size_t) row];
    auto& c = working.channels[(size_t) rr.channel].captures[(size_t) rr.index];
    if (col == colChannel)
    {
        const auto name = text.trim();
        if (name.isEmpty()) return;
        int target = working.findChannel (name.toStdString());
        if (target < 0) target = ChannelManager::addChannel (working, name.toStdString());
        if (target >= 0 && target != rr.channel) ChannelManager::moveCapture (working, rr.channel, rr.index, target);
    }
    else if (col >= colFirstKnob && col < colFirstKnob + kNumControls)
    {
        const int k = col - colFirstKnob;
        const auto t = text.trim().trimCharactersAtStart ("~");
        if (t.isEmpty()) { c.coords[(size_t) k] = std::nan (""); c.source[(size_t) k] = CoordSource::Unknown; }
        else { c.coords[(size_t) k] = juce::jlimit (0.0, 10.0, t.getDoubleValue()); c.source[(size_t) k] = CoordSource::User; }
    }
    else if (col == colTrim)
        c.trimDb = juce::jlimit (-24.0, 24.0, text.getDoubleValue());
    markDirty();
    refresh();
}
} // namespace namknobs
