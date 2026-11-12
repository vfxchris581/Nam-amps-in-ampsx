#pragma once
// Persistent description of one virtual amplifier: identity, channels, the NAM captures in
// each channel, their knob mappings (detected, estimated or user-calibrated) and settings.

#include <optional>
#include <string>
#include <vector>
#include <json.hpp>
#include "NAMMetadata/CaptureMetadata.h"
#include "ProfileAnalyzer/ProfileAnalyzer.h"

namespace namified
{
enum class CoordSource : int { Unknown = 0, Detected = 1, Estimated = 2, User = 3 };

struct CaptureEntry
{
    std::string id;                 // stable uuid-ish
    std::string path;               // absolute path of the .nam file (inside the library)
    CaptureMetadata meta;
    KnobCoords coords = unknownCoords();
    std::array<CoordSource, kNumControls> source {};
    double trimDb = 0.0;            // manual level calibration
    bool enabled = true;
    std::optional<CaptureAnalysis> analysis;
};

struct ChannelDefinition
{
    std::string name;
    std::vector<CaptureEntry> captures;
    KnobCoords savedKnobs { 5, 5, 5, 5, 5, 5, 5, 5 };
};

struct AmpDefinition
{
    std::string id;
    std::string name;
    std::string manufacturer;
    std::string model;
    std::string identityKey;        // normalised "make|model"; captures with another key never join
    std::vector<ChannelDefinition> channels;
    std::string ir1Path, ir2Path;   // default cabinet for this amp (optional)
    std::string notes;

    int totalCaptures() const;
    int findChannel (const std::string& name) const; // -1 if none (case/format-insensitive)
};

nlohmann::json toJson (const AmpDefinition& a);
AmpDefinition ampFromJson (const nlohmann::json& j);

std::string makeId();
} // namespace namified
