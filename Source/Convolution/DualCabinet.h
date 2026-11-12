#pragma once
// Two independent IR slots blended into one cabinet (two cabs or two mics at once).
// Global: blend, cabinet level, mono/stereo output, bypass. Phase alignment between the two
// IRs is measured by cross-correlation and applied as a delay on the earlier one.

#include <atomic>
#include "IRLoader/IRSlot.h"

namespace namified
{
class DualCabinet
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    IRSlot& slot (int i) noexcept { return i == 0 ? slot1 : slot2; }

    struct Global
    {
        bool enabled = true;
        float blend = 0.5f;      // 0 = slot 1 only, 0.5 = both at full, 1 = slot 2 only
        float levelDb = 0.0f;
        bool stereo = true;
        bool autoAlign = true;
    };
    void setGlobal (const Global& g) noexcept { global = g; }
    void setSlotParams (int i, IRSlot::Params p) noexcept;

    /** Audio thread: mono input → stereo output (out must have 2 channels). */
    void process (const float* monoIn, juce::AudioBuffer<float>& out, int n) noexcept;

    struct Alignment { float slot1DelayMs = 0, slot2DelayMs = 0; bool polarityMismatch = false; float correlation = 0; };
    /** Message thread: cross-correlates the two loaded IRs (±3 ms search). */
    Alignment computeAlignment() const;
    /** Message thread: recompute + store the alignment used by process(). */
    void updateAlignment();
    Alignment getAlignment() const noexcept { return alignment; }

private:
    IRSlot slot1, slot2;
    IRSlot::Params p1, p2;
    Global global;
    Alignment alignment;                         // message thread
    std::atomic<float> align1 { 0.0f }, align2 { 0.0f }; // read by the audio thread
    juce::AudioBuffer<float> b1, b2;
    juce::SmoothedValue<float> w1, w2, level;
};
} // namespace namified
