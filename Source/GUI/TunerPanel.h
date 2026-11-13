#pragma once
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namknobs
{
/** Chromatic guitar tuner, shown over the pages from the header's Tuner button.
    Reads the raw guitar input (before any gain), so it works whatever the amp is doing.
    Optionally mutes the output while it is open. */
class TunerPanel : public juce::Component, private juce::Timer
{
public:
    explicit TunerPanel (NamKnobsProcessor& p);
    ~TunerPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    std::function<void()> onClose;

    /** For tests / screenshots: analyse this buffer instead of the live input. */
    void analyse (const float* x, int n, double sampleRate);

private:
    void timerCallback() override;
    void update (double hz, double clarity);

    NamKnobsProcessor& proc;
    juce::ToggleButton mute { "Mute output while tuning" };
    juce::TextButton done { "Done" }, refDown { "-" }, refUp { "+" };
    std::vector<float> ring = std::vector<float> (32768, 0.0f), window;
    int writePos = 0, filled = 0;

    double a4 = 440.0;
    bool hasNote = false;
    double shownHz = 0.0, shownCents = 0.0;
    int shownMidi = -1, silentTicks = 0;
    juce::Rectangle<int> card, noteArea, meterArea, stringsArea, refArea;
};
} // namespace namknobs
