#include "NamKnobsLookAndFeel.h"
#include "BinaryData.h"

namespace namknobs
{
namespace ui
{
static juce::Typeface::Ptr tfRegular()
{
    static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize);
    return t;
}
static juce::Typeface::Ptr tfSemi()
{
    static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
    return t;
}
static juce::Typeface::Ptr tfBrand()
{
    static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
    return t;
}

juce::Font body (float h) { return juce::Font (juce::FontOptions (tfRegular()).withHeight (h)); }
juce::Font semi (float h) { return juce::Font (juce::FontOptions (tfSemi()).withHeight (h)); }
juce::Font brand (float h) { return juce::Font (juce::FontOptions (tfBrand()).withHeight (h)); }

void drawTolex (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xff0d0e0f));
    g.fillRect (r);
    // woven grain: deterministic speckle
    juce::Random rng (42);
    for (int i = 0; i < (int) (r.getWidth() * r.getHeight() / 55); ++i)
    {
        const float x = r.getX() + rng.nextFloat() * r.getWidth(), y = r.getY() + rng.nextFloat() * r.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.018f + 0.02f * rng.nextFloat()));
        g.fillRect (x, y, 1.2f, 1.2f);
    }
    juce::ColourGradient vignette (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                   juce::Colours::black.withAlpha (0.55f), r.getX(), r.getY(), true);
    g.setGradientFill (vignette);
    g.fillRect (r);
}

void drawFaceplate (juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::Path plate;
    plate.addRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0, 3).expanded (1), 7.0f);

    juce::ColourGradient grad (gold.brighter (0.25f), r.getX(), r.getY(), goldDark, r.getX(), r.getBottom(), false);
    grad.addColour (0.45, gold);
    grad.addColour (0.55, gold.darker (0.05f));
    g.setGradientFill (grad);
    g.fillPath (plate);

    // brushed lines
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (plate);
        juce::Random rng (7);
        for (float y = r.getY(); y < r.getBottom(); y += 1.0f)
        {
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.025f + 0.03f * rng.nextFloat()));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawLine (r.getX() + 6, r.getY() + 1.0f, r.getRight() - 6, r.getY() + 1.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);

    // screws
    for (auto p : { r.getTopLeft().translated (14, 14), r.getTopRight().translated (-14, 14),
                    r.getBottomLeft().translated (14, -14), r.getBottomRight().translated (-14, -14) })
    {
        const auto sc = juce::Rectangle<float> (9, 9).withCentre (p);
        juce::ColourGradient sg (juce::Colour (0xffe9e4d8), sc.getX(), sc.getY(), juce::Colour (0xff6d6455), sc.getRight(), sc.getBottom(), false);
        g.setGradientFill (sg);
        g.fillEllipse (sc);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawLine (sc.getX() + 2, sc.getCentreY() + 1.5f, sc.getRight() - 2, sc.getCentreY() - 1.5f, 1.2f);
    }
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (textDim);
        g.setFont (semi (11.5f).withExtraKerningFactor (0.12f));
        g.drawText (title.toUpperCase(), r.reduced (16, 10).removeFromTop (16), juce::Justification::topLeft);
    }
}

void drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool on)
{
    if (on)
    {
        g.setColour (c.withAlpha (0.16f));
        g.fillEllipse (r.expanded (r.getWidth() * 0.35f));
    }
    juce::ColourGradient lg (on ? c.brighter (0.6f) : juce::Colour (0xff3a3b3d), r.getCentreX(), r.getY(),
                             on ? c.darker (0.3f) : juce::Colour (0xff161718), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (lg);
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (r, 1.0f);
}
} // namespace ui

NamKnobsLookAndFeel::NamKnobsLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, ui::bg);
    setColour (juce::Label::textColourId, ui::text);
    setColour (juce::TextButton::buttonColourId, ui::panelHi);
    setColour (juce::TextButton::textColourOffId, ui::text);
    setColour (juce::TextButton::textColourOnId, ui::ink);
    setColour (juce::ComboBox::backgroundColourId, ui::panelHi);
    setColour (juce::ComboBox::textColourId, ui::text);
    setColour (juce::ComboBox::outlineColourId, ui::line);
    setColour (juce::ComboBox::arrowColourId, ui::textDim);
    setColour (juce::PopupMenu::backgroundColourId, ui::panelHi);
    setColour (juce::PopupMenu::textColourId, ui::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::gold.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, ui::ink);
    setColour (juce::TextEditor::backgroundColourId, ui::bg);
    setColour (juce::TextEditor::textColourId, ui::text);
    setColour (juce::TextEditor::outlineColourId, ui::line);
    setColour (juce::TextEditor::focusedOutlineColourId, ui::gold);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, ui::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, ui::line.brighter (0.3f));
    setColour (juce::TableHeaderComponent::backgroundColourId, ui::panelHi);
    setColour (juce::TableHeaderComponent::textColourId, ui::textDim);
    setColour (juce::TableHeaderComponent::outlineColourId, ui::line);
    setColour (juce::AlertWindow::backgroundColourId, ui::panel);
    setColour (juce::AlertWindow::textColourId, ui::text);
}

juce::Typeface::Ptr NamKnobsLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() || f.getTypefaceName() == "<Sans-Serif>")
        return f.isBold() ? ui::tfSemi() : ui::tfRegular();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void NamKnobsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                             float end, juce::Slider& s)
{
    const bool onPlate = (bool) s.getProperties().getWithDefault ("faceplate", false);
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = std::min (bounds.getWidth(), bounds.getHeight());
    const auto r = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
    const float angle = start + pos * (end - start);
    const auto c = r.getCentre();

    // scale ticks (0..10)
    g.setColour (onPlate ? ui::ink.withAlpha (0.75f) : ui::textDim.withAlpha (0.6f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * i / 10.0f;
        const float r1 = size * 0.5f, r2 = size * (i % 5 == 0 ? 0.43f : 0.455f);
        g.drawLine (c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), c.x + r2 * std::sin (a), c.y - r2 * std::cos (a),
                    i % 5 == 0 ? 1.6f : 1.0f);
    }

    // value arc for off-plate knobs
    const auto knob = r.reduced (size * 0.12f);
    if (! onPlate)
    {
        juce::Path track, val;
        const float ar = size * 0.5f - 7.0f;
        track.addCentredArc (c.x, c.y, ar, ar, 0, start, end, true);
        val.addCentredArc (c.x, c.y, ar, ar, 0, start, angle, true);
        g.setColour (ui::line);
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (ui::amber.withAlpha (s.isEnabled() ? 1.0f : 0.3f));
        g.strokePath (val, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (knob.translated (0, size * 0.04f).expanded (1.5f));

    // knurled bakelite skirt
    juce::Path skirt;
    const int teeth = 36;
    for (int i = 0; i <= teeth * 2; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * i / (teeth * 2) + angle;
        const float rr = knob.getWidth() * 0.5f * (i % 2 == 0 ? 1.0f : 0.965f);
        const auto p = juce::Point<float> (c.x + rr * std::sin (a), c.y - rr * std::cos (a));
        if (i == 0) skirt.startNewSubPath (p); else skirt.lineTo (p);
    }
    skirt.closeSubPath();
    juce::ColourGradient kg (juce::Colour (0xff3b3d40), knob.getX(), knob.getY(), juce::Colour (0xff050506), knob.getRight(), knob.getBottom(), false);
    g.setGradientFill (kg);
    g.fillPath (skirt);

    // machined cap
    const auto cap = knob.reduced (knob.getWidth() * 0.2f);
    juce::ColourGradient cg (juce::Colour (0xfff2f1ee), cap.getX(), cap.getY(), juce::Colour (0xff7d7f83), cap.getRight(), cap.getBottom(), false);
    cg.addColour (0.5, juce::Colour (0xffc4c6c9));
    g.setGradientFill (cg);
    g.fillEllipse (cap);
    juce::Random rng (3);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (int i = 1; i < 6; ++i) g.drawEllipse (cap.reduced (cap.getWidth() * 0.08f * i), 0.6f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (cap, 1.0f);

    // pointer
    const float pr1 = knob.getWidth() * 0.08f, pr2 = knob.getWidth() * 0.47f;
    g.setColour (juce::Colours::white);
    g.drawLine (c.x + pr1 * std::sin (angle), c.y - pr1 * std::cos (angle), c.x + pr2 * std::sin (angle), c.y - pr2 * std::cos (angle), 2.6f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawLine (c.x + knob.getWidth() * 0.32f * std::sin (angle), c.y - knob.getWidth() * 0.32f * std::cos (angle),
                c.x + pr2 * std::sin (angle), c.y - pr2 * std::cos (angle), 0.8f);
}

void NamKnobsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const float d = std::min (14.0f, r.getHeight() * 0.5f);
    const auto led = r.removeFromLeft (d + 10.0f).withSizeKeepingCentre (d, d);
    ui::drawLed (g, led, b.getProperties().getWithDefault ("ledRed", false) ? ui::red : ui::amber, on);
    g.setColour (on || highlighted ? ui::text : ui::textDim);
    g.setFont (ui::semi (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (4), juce::Justification::centredLeft);
}

void NamKnobsLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    if (b.getProperties().getWithDefault ("channel", false))
    {
        // illuminated channel switch
        juce::ColourGradient gr (on ? juce::Colour (0xff2a2117) : juce::Colour (0xff1b1c1e), r.getX(), r.getY(),
                                 on ? juce::Colour (0xff17130e) : juce::Colour (0xff0f1011), r.getX(), r.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? ui::amber : ui::line.darker (0.3f));
        g.drawRoundedRectangle (r, 4.0f, on ? 1.4f : 1.0f);
        if (on)
        {
            g.setColour (ui::amber.withAlpha (0.18f));
            g.fillRoundedRectangle (r.reduced (2), 3.0f);
        }
        return;
    }
    auto base = on ? ui::gold : (b.findColour (juce::TextButton::buttonColourId));
    if (highlighted) base = base.brighter (0.08f);
    if (down) base = base.darker (0.1f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? ui::goldDark : ui::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void NamKnobsLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? ui::gold : ui::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (ui::textDim);
    g.fillPath (arrow);
}

void NamKnobsLookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool front = b.isFrontTab();
    g.setColour (front ? ui::text : (over ? ui::text.withAlpha (0.8f) : ui::textDim));
    g.setFont (ui::semi (12.5f).withExtraKerningFactor (0.12f));
    g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred);
    if (front)
    {
        g.setColour (ui::gold);
        g.fillRoundedRectangle (r.removeFromBottom (2.5f).reduced (r.getWidth() * 0.22f, 0), 1.2f);
    }
}

void NamKnobsLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
    const float cy = y + h * 0.5f;
    g.setColour (ui::line);
    g.fillRoundedRectangle ((float) x, cy - 2, (float) w, 4, 2);
    g.setColour (ui::amber);
    g.fillRoundedRectangle ((float) x, cy - 2, pos - x, 4, 2);
    g.setColour (ui::text);
    g.fillEllipse (pos - 7, cy - 7, 14, 14);
}
} // namespace namknobs
