#pragma once
// Full presets (amp + channel + all parameters + IRs) and dual-IR cabinet presets.

#include <juce_data_structures/juce_data_structures.h>

namespace namified
{
class PresetManager
{
public:
    explicit PresetManager (juce::File presetsDir, juce::File cabPresetsDir);

    juce::Array<juce::File> listPresets() const;
    bool savePreset (const juce::String& name, const juce::ValueTree& state);
    juce::ValueTree loadPreset (const juce::File& file) const;
    bool deletePreset (const juce::File& file);
    bool renamePreset (const juce::File& file, const juce::String& newName);

    struct CabPreset
    {
        juce::String name;
        juce::String ir1Path, ir2Path;
        juce::NamedValueSet values; // parameter id → value
    };
    juce::Array<juce::File> listCabPresets() const;
    bool saveCabPreset (const CabPreset& p);
    bool loadCabPreset (const juce::File& f, CabPreset& out) const;

    static juce::String sanitise (const juce::String& name);
    static constexpr const char* kPresetExt = ".namified";
    static constexpr const char* kCabExt = ".namcab";

private:
    juce::File dir, cabDir;
};
} // namespace namified
