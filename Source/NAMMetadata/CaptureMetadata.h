#pragma once
// NAM metadata reading + capture identity / knob-setting detection.

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <filesystem>

namespace namvx
{
/** The eight amplifier controls Nam VX builds for every virtual amp. */
enum class Control : int { Gain = 0, Volume, Bass, Middle, Treble, Presence, Resonance, Master, Count };
constexpr int kNumControls = (int) Control::Count;
const char* controlName (Control c) noexcept;
const char* controlId (Control c) noexcept;

/** Knob coordinates on a 0..10 scale. NaN means "unknown for this capture". */
using KnobCoords = std::array<double, kNumControls>;
inline KnobCoords unknownCoords() noexcept { KnobCoords k; k.fill (std::nan ("")); return k; }
inline bool isKnown (double v) noexcept { return ! std::isnan (v); }

struct CaptureMetadata
{
    std::string fileName;          // without extension
    std::string name;              // metadata "name" or file name
    std::string manufacturer;      // metadata "gear_make"
    std::string model;             // metadata "gear_model"
    std::string gearType;          // amp, pedal, amp_cab, ...
    std::string toneType;          // clean, crunch, hi_gain, fuzz
    std::string modeledBy;
    std::string architecture;
    std::string napVersion;
    std::optional<double> loudnessDb;
    std::optional<double> inputLevelDbu, outputLevelDbu;
    double expectedSampleRate = -1.0;

    /** Explicit channel detected in metadata/name ("ch2", "lead", ...). Empty = none. */
    std::string channelHint;
    int channelNumberHint = 0;     // 1-based if a number was found

    /** Knob settings detected from the capture name / file name. */
    KnobCoords knobs = unknownCoords();

    /** True when manufacturer+model came from NAM metadata (trusted). */
    bool identityFromMetadata = false;
    /** Best guess (from the file name) when metadata lacks identity — must be user-confirmed. */
    std::string suggestedManufacturer, suggestedModel;

    bool hasIdentity() const noexcept { return ! manufacturer.empty() && ! model.empty(); }
    /** Normalised identity key used for grouping: "make|model". Empty when unknown. */
    std::string identityKey() const;
};

/** Normalise a free-text name for comparison (lower-case alphanumerics, single spaces). */
std::string normaliseName (const std::string& s);

/** Parse only the JSON header/metadata of a .nam file (does not build the network). */
CaptureMetadata readCaptureMetadata (const std::filesystem::path& file);

/** Exposed for testing: parses knob/channel tokens from arbitrary text into 'meta'. */
void parseSettingsFromText (const std::string& text, CaptureMetadata& meta);

/** Exposed for testing: guess make/model from a capture name with settings tokens removed. */
void guessIdentityFromText (const std::string& text, CaptureMetadata& meta);
} // namespace namvx
