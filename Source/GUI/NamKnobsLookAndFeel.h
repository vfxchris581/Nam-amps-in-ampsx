#pragma once
// Premium amp styling: black tolex, brushed-gold faceplate, knurled bakelite knobs with
// machined caps, amber channel LEDs, Inter / Poppins typography.

#include <juce_gui_basics/juce_gui_basics.h>

namespace namknobs
{
namespace ui
{
    const juce::Colour bg        { 0xff111214 };
    const juce::Colour panel     { 0xff1a1c1f };
    const juce::Colour panelHi   { 0xff23262a };
    const juce::Colour line      { 0xff2e3237 };
    const juce::Colour text      { 0xffe9e6df };
    const juce::Colour textDim   { 0xff8f8b83 };
    const juce::Colour gold      { 0xffcdb57d };
    const juce::Colour goldDark  { 0xff9c8452 };
    const juce::Colour ink       { 0xff1b1712 };
    const juce::Colour amber     { 0xffffb648 };
    const juce::Colour red       { 0xffe5483c };
    const juce::Colour green     { 0xff63d18c };

    juce::Font body (float h);
    juce::Font semi (float h);
    juce::Font brand (float h);

    /** Black tolex with a subtle grain. */
    void drawTolex (juce::Graphics& g, juce::Rectangle<float> r);
    /** Brushed-gold faceplate with bevel and screws. */
    void drawFaceplate (juce::Graphics& g, juce::Rectangle<float> r);
    /** Dark recessed panel for secondary pages. */
    void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {});
    void drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool on);
}

class NamKnobsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NamKnobsLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return ui::semi (std::min (14.0f, h * 0.48f)); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return ui::semi (14.0f); }
    juce::Font getPopupMenuFont() override { return ui::body (14.0f); }
    juce::Font getLabelFont (juce::Label&) override { return ui::body (13.0f); }
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int) override { return 150; }
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override {}
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};
} // namespace namknobs
