#pragma once
// The automatic amp library. Every import becomes a saved amplifier instrument:
//   <root>/Amps/<id>.json          amp definitions (channels, captures, mappings)
//   <root>/Captures/<id>/*.nam     library copies of the imported captures
//   <root>/Presets/*.json          full presets (amp + knobs + tube/harmonic/cab settings)
//   <root>/CabPresets/*.json       dual-IR cabinet presets
// Grouping rule: captures join an amp only when their manufacturer+model identity matches
// exactly (normalised). Captures without identity are returned as "pending" for manual
// assignment — unrelated amps are never combined automatically.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "AmplifierEngine/AmpDefinition.h"

namespace namknobs
{
struct PendingCapture
{
    std::string path;
    CaptureMetadata meta;
    std::optional<CaptureAnalysis> analysis;
};

struct ImportResult
{
    int imported = 0, duplicates = 0;
    std::vector<std::string> ampIds;      // amps created or extended
    std::vector<PendingCapture> pending;  // need manufacturer/model from the user
    std::vector<std::string> errors;
};

class AmpLibrary
{
public:
    explicit AmpLibrary (std::filesystem::path rootDir);

    const std::filesystem::path& getRoot() const noexcept { return root; }
    std::filesystem::path presetsDir() const { return root / "Presets"; }
    std::filesystem::path cabPresetsDir() const { return root / "CabPresets"; }

    void reload();
    const std::vector<AmpDefinition>& getAmps() const noexcept { return amps; }
    const AmpDefinition* find (const std::string& id) const;

    using Progress = std::function<bool (int done, int total, const std::string& file)>; // return false to cancel

    /** Imports .nam files (folders allowed): reads metadata, analyses each capture, groups by
        identity, builds channels, estimates settings, saves. Blocking: call from a worker thread. */
    ImportResult importFiles (const std::vector<std::filesystem::path>& files, Progress progress = {},
                              bool analyse = true);

    /** Manual assignment of a capture that had no identity. Returns the amp id. */
    std::string assignPending (const PendingCapture& p, const std::string& manufacturer,
                               const std::string& model, const std::string& channel = {});

    /** Creates a blend amp from NAM models of any amps (2..4). Blocking (analyses each model).
        Returns the new amp id, or empty with 'error' filled. */
    std::string createBlend (const std::string& name, const std::vector<std::filesystem::path>& files, std::string& error);

    /** Loads one NAM file into a blend slot (0..7), replacing what was there. With an empty
        ampId a new blend is created. Blocking (analyses the model). Returns the amp id. */
    std::string setBlendSlot (const std::string& ampId, int slot, const std::filesystem::path& file,
                              const std::string& newBlendName, std::string& error);
    bool clearBlendSlot (const std::string& ampId, int slot);
    /** Empties every loader of a blend and deletes its library copies of the captures. */
    bool clearAllBlendSlots (const std::string& ampId);

    bool save (AmpDefinition amp);       // insert or replace by id
    bool remove (const std::string& id); // deletes definition and library capture copies
    bool rename (const std::string& id, const std::string& newName);

private:
    std::string placeInLibrary (const std::string& ampId, const std::filesystem::path& src);
    AmpDefinition& getOrCreate (const std::string& manufacturer, const std::string& model, const std::string& key);
    void writeAmp (const AmpDefinition& a);

    std::filesystem::path root;
    std::vector<AmpDefinition> amps;
};
} // namespace namknobs
