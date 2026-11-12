#include "PresetManager.h"

namespace namified
{
PresetManager::PresetManager (juce::File p, juce::File c) : dir (std::move (p)), cabDir (std::move (c))
{
    dir.createDirectory();
    cabDir.createDirectory();
}

juce::String PresetManager::sanitise (const juce::String& name)
{
    auto s = juce::File::createLegalFileName (name.trim());
    return s.isEmpty() ? juce::String ("Untitled") : s;
}

juce::Array<juce::File> PresetManager::listPresets() const
{
    auto files = dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kPresetExt);
    files.sort();
    return files;
}

bool PresetManager::savePreset (const juce::String& name, const juce::ValueTree& state)
{
    auto copy = state.createCopy();
    copy.setProperty ("presetName", name, nullptr);
    if (auto xml = copy.createXml())
        return xml->writeTo (dir.getChildFile (sanitise (name) + kPresetExt));
    return false;
}

juce::ValueTree PresetManager::loadPreset (const juce::File& f) const
{
    if (auto xml = juce::XmlDocument::parse (f)) return juce::ValueTree::fromXml (*xml);
    return {};
}

bool PresetManager::deletePreset (const juce::File& f) { return f.existsAsFile() && f.deleteFile(); }

bool PresetManager::renamePreset (const juce::File& f, const juce::String& newName)
{
    auto tree = loadPreset (f);
    if (! tree.isValid()) return false;
    if (! savePreset (newName, tree)) return false;
    const auto target = dir.getChildFile (sanitise (newName) + kPresetExt);
    if (target != f) f.deleteFile();
    return true;
}

juce::Array<juce::File> PresetManager::listCabPresets() const
{
    auto files = cabDir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kCabExt);
    files.sort();
    return files;
}

bool PresetManager::saveCabPreset (const CabPreset& p)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("format", "namified-cab");
    o->setProperty ("name", p.name);
    o->setProperty ("ir1", p.ir1Path);
    o->setProperty ("ir2", p.ir2Path);
    juce::DynamicObject::Ptr vals = new juce::DynamicObject();
    for (auto& nv : p.values) vals->setProperty (nv.name, nv.value);
    o->setProperty ("values", juce::var (vals.get()));
    return cabDir.getChildFile (sanitise (p.name) + kCabExt).replaceWithText (juce::JSON::toString (juce::var (o.get())));
}

bool PresetManager::loadCabPreset (const juce::File& f, CabPreset& out) const
{
    const auto v = juce::JSON::parse (f);
    if (! v.isObject()) return false;
    out.name = v["name"].toString();
    out.ir1Path = v["ir1"].toString();
    out.ir2Path = v["ir2"].toString();
    out.values.clear();
    if (auto* obj = v["values"].getDynamicObject())
        for (auto& nv : obj->getProperties()) out.values.set (nv.name, nv.value);
    return true;
}
} // namespace namified
