#pragma once
// Offline "listening" analysis of a NAM capture: latency, polarity, loudness, compression,
// harmonic content and tonal balance. Used for loudness compensation, phase alignment,
// and for estimating knob settings that the file names/metadata do not reveal.

#include <array>
#include <vector>
#include "NAMEngine/NamModel.h"
#include "NAMMetadata/CaptureMetadata.h"

namespace namvx
{
struct CaptureAnalysis
{
    bool valid = false;
    double sampleRate = 48000.0;
    int latencySamples = 0;      // at the model's own rate
    int polarity = 1;            // +1 or -1 (sign of the impulse response peak)
    double loudnessDb = -18.0;   // output RMS for a -18 dBFS RMS broadband multitone
    double compression = 1.0;    // dB out per dB in between -36 and -6 dBFS (1 = linear)
    double thdDb = -60.0;        // total harmonic distortion, 220 Hz @ -12 dBFS
    double evenOddDb = 0.0;      // even/odd harmonic energy ratio (dB)
    // Band levels relative to the overall average (dB): resonance(60-120), bass(120-300),
    // mid(300-1.5k), treble(1.5k-4k), presence(4k-8k)
    std::array<double, 5> bandsDb {};
};

class ProfileAnalyzer
{
public:
    /** Runs the analysis (blocking, ~0.1-1 s). The model is re-prepared at its own rate. */
    static CaptureAnalysis analyse (NamModel& model);

    /** Fill unknown knob coordinates of a channel from the analyses where the captures clearly
        differ. Returns per-capture flags marking which coordinates were estimated. */
    static std::vector<std::array<bool, kNumControls>>
        estimateCoordinates (std::vector<KnobCoords>& coords, const std::vector<CaptureAnalysis>& analyses);

    /** Tone feature associated with a control (used by estimateCoordinates and tests). */
    static double featureFor (Control c, const CaptureAnalysis& a) noexcept;
};
} // namespace namvx
