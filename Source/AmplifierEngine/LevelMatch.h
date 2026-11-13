#pragma once
// Level matching of NAM captures.
//
// Every capture is played a short guitar-like phrase (single notes and a power chord at a normal
// DI level) and its loudness is measured with the K-weighting of ITU-R BS.1770 (how loud it
// *sounds*, not just its RMS). Each capture then gets the gain that brings it to the same
// reference loudness, so a clean capture and a high-gain capture play at the same volume and
// blends don't jump when the knobs move.

#include <vector>

namespace namknobs
{
struct LevelMatch
{
    static constexpr double kReferenceDb = -18.0;  // target K-weighted loudness of every capture
    static constexpr double kMaxCorrectionDb = 24.0;

    /** ~0.9 s guitar phrase (E2, A2+E3 power chord, B3), peaks around -9 dBFS. */
    static std::vector<double> testPhrase (double sampleRate);
    /** K-weighted mean-square loudness in dB (relative, no LUFS offset). */
    static double loudnessDb (const std::vector<double>& x, double sampleRate);
    /** Gain (dB) that brings a capture measured at 'loudnessDb' to the reference. */
    static double correctionDb (double loudnessDb);
};
} // namespace namknobs
