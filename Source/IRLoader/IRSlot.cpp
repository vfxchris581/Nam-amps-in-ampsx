#include "IRSlot.h"

namespace namvx
{
IRSlot::IRSlot() = default;

juce::String IRSlot::loadFile (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) return "Could not read impulse response: " + f.getFileName();
    if (reader->lengthInSamples <= 0) return "Empty impulse response: " + f.getFileName();

    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 2.0)); // max 2 s
    const int chans = (int) std::min<unsigned int> (2, reader->numChannels);
    juce::AudioBuffer<float> buf (chans, len);
    reader->read (&buf, 0, len, 0, true, chans > 1);
    return loadBuffer (std::move (buf), reader->sampleRate, f);
}

juce::String IRSlot::loadBuffer (juce::AudioBuffer<float> ir, double irRate, const juce::File& source)
{
    if (ir.getNumSamples() == 0 || ir.getNumChannels() == 0) return "Empty impulse response";
    const int chans = std::min (2, ir.getNumChannels());

    // Energy normalisation (every IR plays back at a comparable level regardless of mic/cab).
    double energy = 0.0;
    for (int c = 0; c < chans; ++c)
        for (int i = 0; i < ir.getNumSamples(); ++i) energy += (double) ir.getSample (c, i) * ir.getSample (c, i);
    energy /= chans;
    if (energy <= 1.0e-20) return "Silent impulse response";
    // The convolution engine resamples the IR to the session rate preserving its frequency
    // response (amplitude scales with irRate/hostRate). Normalising to an energy of 48k/irRate
    // makes every IR play back at the same broadband level whatever rate it was stored at.
    const float norm = (float) std::sqrt ((48000.0 / irRate) / energy);
    ir.applyGain (norm);

    // Onset = first sample reaching -20 dB of the peak.
    float peak = 0.0f;
    for (int c = 0; c < chans; ++c) peak = std::max (peak, ir.getMagnitude (c, 0, ir.getNumSamples()));
    int onset = 0;
    for (int i = 0; i < ir.getNumSamples() && onset == 0; ++i)
        for (int c = 0; c < chans; ++c)
            if (std::abs (ir.getSample (c, i)) >= 0.1f * peak) { onset = i; break; }

    analysisCopy.setSize (1, ir.getNumSamples());
    analysisCopy.clear();
    for (int c = 0; c < chans; ++c) analysisCopy.addFrom (0, 0, ir, c, 0, ir.getNumSamples(), 1.0f / chans);

    info.file = source;
    info.sampleRate = irRate;
    info.numChannels = chans;
    info.lengthSamples = ir.getNumSamples();
    info.onsetMs = 1000.0 * onset / irRate;

    if (ir.getNumChannels() > 2) ir.setSize (2, ir.getNumSamples(), true);
    stereoIR = chans == 2;
    conv.loadImpulseResponse (std::move (ir), irRate,
                              chans == 2 ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                              juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    loadedFlag = true;
    return {};
}

void IRSlot::clear()
{
    loadedFlag = false;
    info = {};
    analysisCopy.setSize (0, 0);
}

void IRSlot::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    conv.prepare ({ spec.sampleRate, spec.maximumBlockSize, 2 });
    delay.setMaximumDelayInSamples ((int) (spec.sampleRate * 0.025) + 8);
    delay.prepare ({ spec.sampleRate, spec.maximumBlockSize, 2 });
    work.setSize (2, (int) spec.maximumBlockSize);
    for (auto* s : { &gainL, &gainR }) s->reset (spec.sampleRate, 0.03);
    delaySmoothed.reset (spec.sampleRate, 0.05);
    reset();
}

void IRSlot::reset()
{
    conv.reset();
    delay.reset();
}

void IRSlot::process (const float* monoIn, juce::AudioBuffer<float>& out, int n) noexcept
{
    out.clear (0, n);
    if (! loadedFlag || ! params.enabled) return;

    const bool stereo = stereoIR.load();
    work.copyFrom (0, 0, monoIn, n);
    if (stereo) work.copyFrom (1, 0, monoIn, n);
    {
        juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), stereo ? 2 : 1, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    if (! stereo) work.copyFrom (1, 0, work, 0, 0, n);

    // equal-power pan (balance for stereo IRs)
    const float p = juce::jlimit (-1.0f, 1.0f, params.pan);
    const float angle = (p + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float g = juce::Decibels::decibelsToGain (params.levelDb, -100.0f) * (params.invert ? -1.0f : 1.0f);
    gainL.setTargetValue (g * std::cos (angle) * juce::MathConstants<float>::sqrt2);
    gainR.setTargetValue (g * std::sin (angle) * juce::MathConstants<float>::sqrt2);
    delaySmoothed.setTargetValue ((float) ((params.delayMs + params.alignMs) * 0.001 * sampleRate));

    auto* l = work.getWritePointer (0);
    auto* r = work.getWritePointer (1);
    auto* ol = out.getWritePointer (0);
    auto* orr = out.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float d = delaySmoothed.getNextValue();
        delay.setDelay (d);
        delay.pushSample (0, l[i]);
        delay.pushSample (1, r[i]);
        ol[i] = delay.popSample (0) * gainL.getNextValue();
        orr[i] = delay.popSample (1) * gainR.getNextValue();
    }
}
} // namespace namvx
