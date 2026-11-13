#pragma once
// Guitar tuner pitch detection (YIN).
//
// A coarse YIN search runs on a 4x-decimated copy (cheap, robust against octave errors),
// then the period is refined on the full-rate signal with parabolic interpolation, which
// gives a resolution well below one cent across the guitar range (drop tunings to high frets).

#include <string>

namespace namknobs
{
class PitchDetector
{
public:
    struct Result
    {
        double hz = 0.0;      // 0 = no pitch (silence or noise)
        double clarity = 0.0; // 0..1 (1 = perfectly periodic)
    };
    /** x: the most recent n samples at sampleRate. */
    static Result detect (const float* x, int n, double sampleRate, double minHz = 28.0, double maxHz = 1400.0);

    struct Note
    {
        int midi = 0;
        double cents = 0.0;   // -50..+50 from the nearest note
        std::string name;     // "E", "F#", ...
        int octave = 0;       // scientific pitch (A4 = 440)
    };
    static Note noteFor (double hz, double a4 = 440.0);
    /** Samples the detector needs to see the lowest pitch (two periods + search range). */
    static int samplesNeeded (double sampleRate, double minHz = 28.0);
};
} // namespace namknobs
