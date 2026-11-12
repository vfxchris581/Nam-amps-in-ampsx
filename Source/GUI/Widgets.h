#pragma once
// Small reusable controls bound to APVTS parameters.

#include <juce_audio_processors/juce_audio_processors.h>
#include "NamifiedLookAndFeel.h"

namespace namified
{
using APVTS = juce::AudioProcessorValueTreeState;

/** Rotary knob + caption + value read-out, attached to a parameter. */
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& caption, bool onFaceplate = false)
        : name (caption), faceplate (onFaceplate)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true);
        slider.getProperties().set ("faceplate", onFaceplate);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setDoubleClickReturnValue (true, (double) state.getParameterRange (paramId).convertFrom0to1 (
                                                         state.getParameter (paramId)->getDefaultValue()));
        addAndMakeVisible (slider);
        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        slider.onValueChange = [this] { repaint(); };
    }

    void setBadge (const juce::String& b, juce::Colour c) { badge = b; badgeColour = c; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto cap = r.removeFromBottom (faceplate ? 34.0f : 30.0f);
        g.setColour (faceplate ? ui::ink : ui::textDim);
        g.setFont (ui::semi (faceplate ? 12.5f : 11.5f).withExtraKerningFactor (0.14f));
        g.drawFittedText (name.toUpperCase(), cap.removeFromTop (16).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setFont (ui::body (11.0f));
        g.setColour (faceplate ? ui::ink.withAlpha (0.6f) : ui::text.withAlpha (0.75f));
        g.drawText (slider.getTextFromValue (slider.getValue()), cap.removeFromTop (14), juce::Justification::centred);
        if (badge.isNotEmpty())
        {
            const auto b = juce::Rectangle<float> (30, 13).withCentre ({ r.getCentreX(), r.getY() + 6.0f });
            g.setColour (badgeColour.withAlpha (0.9f));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (ui::ink);
            g.setFont (ui::semi (9.0f));
            g.drawText (badge, b, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom (faceplate ? 34 : 30);
        if (badge.isNotEmpty() || faceplate) r.removeFromTop (12);
        slider.setBounds (r);
    }

    juce::Slider slider;

private:
    juce::String name, badge;
    juce::Colour badgeColour;
    bool faceplate;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

/** LED toggle attached to a bool parameter. */
class Toggle : public juce::ToggleButton
{
public:
    Toggle (APVTS& state, const juce::String& paramId, const juce::String& caption)
    {
        setButtonText (caption);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** Combo attached to a choice parameter. */
class Choice : public juce::ComboBox
{
public:
    Choice (APVTS& state, const juce::String& paramId)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            addItemList (p->choices, 1);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

/** Vertical peak meter. */
class Meter : public juce::Component
{
public:
    void setLevel (float peak) { level = peak; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 2.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = r.reduced (1.5f);
        fill = fill.removeFromBottom (fill.getHeight() * frac);
        g.setColour (db > -1.0f ? ui::red : (db > -12.0f ? ui::amber : ui::green));
        g.fillRoundedRectangle (fill, 1.5f);
    }

private:
    float level = 0.0f;
};
} // namespace namified
