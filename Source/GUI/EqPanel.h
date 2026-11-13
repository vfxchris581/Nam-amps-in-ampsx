#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

namespace namknobs
{
/** Studio EQ: response curve with draggable band nodes over a live spectrum analyser,
    and per-band controls (on, frequency, gain, Q / slope). */
class EqPanel : public juce::Component, private juce::Timer
{
public:
    explicit EqPanel (NamKnobsProcessor& p);
    ~EqPanel() override;
    void refresh() {}
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    EqSettings currentSettings() const;
    float freqToX (double f) const;
    double xToFreq (float x) const;
    float dbToY (double db) const;
    double yToDb (float y) const;
    juce::Point<float> nodePos (int band, const EqSettings& s) const;
    int hitNode (juce::Point<float> p) const;
    void setParam (const char* id, float value);

    NamKnobsProcessor& proc;
    Toggle eqOn;
    Choice position;
    Knob output;
    struct BandUI
    {
        std::unique_ptr<Toggle> on;
        std::unique_ptr<Knob> freq, gain, q;
        std::unique_ptr<Choice> slope;
    };
    std::array<BandUI, 7> bands;
    juce::Rectangle<int> curveArea, controlsArea;
    int dragging = -1, hover = -1;

    // spectrum analyser
    static constexpr int kFftOrder = 12, kFftSize = 1 << kFftOrder;
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ring = std::vector<float> ((size_t) kFftSize, 0.0f);
    std::vector<float> fftData = std::vector<float> ((size_t) kFftSize * 2, 0.0f);
    std::vector<float> spectrumDb = std::vector<float> ((size_t) kFftSize / 2, -100.0f);
    std::vector<float> incoming = std::vector<float> (16384, 0.0f);
    int ringPos = 0;
};
} // namespace namknobs
