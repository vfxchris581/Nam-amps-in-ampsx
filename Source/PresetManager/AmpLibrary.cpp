#include "AmpLibrary.h"

#include <algorithm>
#include <fstream>
#include <set>
#include "ChannelManager/ChannelManager.h"
#include "NAMLoader/NamLoader.h"

namespace fs = std::filesystem;

namespace namknobs
{
AmpLibrary::AmpLibrary (fs::path rootDir) : root (std::move (rootDir))
{
    std::error_code ec;
    for (auto sub : { "Amps", "Captures", "Presets", "CabPresets" }) fs::create_directories (root / sub, ec);
    reload();
}

void AmpLibrary::reload()
{
    amps.clear();
    std::error_code ec;
    for (auto& e : fs::directory_iterator (root / "Amps", ec))
    {
        if (e.path().extension() != ".json") continue;
        try
        {
            std::ifstream in (e.path());
            nlohmann::json j;
            in >> j;
            amps.push_back (ampFromJson (j));
        }
        catch (...) {} // unreadable file: skip, never crash the plugin
    }
    std::sort (amps.begin(), amps.end(), [] (const AmpDefinition& a, const AmpDefinition& b)
               { return normaliseName (a.name) < normaliseName (b.name); });
}

const AmpDefinition* AmpLibrary::find (const std::string& id) const
{
    for (auto& a : amps) if (a.id == id) return &a;
    return nullptr;
}

void AmpLibrary::writeAmp (const AmpDefinition& a)
{
    const auto tmp = root / "Amps" / (a.id + ".json.tmp");
    {
        std::ofstream out (tmp);
        out << toJson (a).dump (2);
    }
    std::error_code ec;
    fs::rename (tmp, root / "Amps" / (a.id + ".json"), ec); // atomic replace
    if (ec) { fs::copy_file (tmp, root / "Amps" / (a.id + ".json"), fs::copy_options::overwrite_existing, ec); fs::remove (tmp, ec); }
}

bool AmpLibrary::save (AmpDefinition amp)
{
    if (amp.id.empty()) amp.id = makeId();
    writeAmp (amp);
    for (auto& a : amps) if (a.id == amp.id) { a = std::move (amp); return true; }
    amps.push_back (std::move (amp));
    return true;
}

bool AmpLibrary::remove (const std::string& id)
{
    std::error_code ec;
    fs::remove (root / "Amps" / (id + ".json"), ec);
    fs::remove_all (root / "Captures" / id, ec);
    const auto before = amps.size();
    amps.erase (std::remove_if (amps.begin(), amps.end(), [&] (const AmpDefinition& a) { return a.id == id; }), amps.end());
    return amps.size() != before;
}

bool AmpLibrary::rename (const std::string& id, const std::string& newName)
{
    for (auto& a : amps)
        if (a.id == id && ! newName.empty()) { a.name = newName; writeAmp (a); return true; }
    return false;
}

std::string AmpLibrary::placeInLibrary (const std::string& ampId, const fs::path& src)
{
    std::error_code ec;
    const auto dir = root / "Captures" / ampId;
    fs::create_directories (dir, ec);
    // Already inside the library?
    const auto canonicalSrc = fs::weakly_canonical (src, ec);
    if (canonicalSrc.string().rfind (fs::weakly_canonical (root, ec).string(), 0) == 0) return src.string();

    auto dst = dir / src.filename();
    for (int n = 2; fs::exists (dst, ec); ++n)
        dst = dir / (src.stem().string() + " (" + std::to_string (n) + ")" + src.extension().string());
    fs::copy_file (src, dst, ec);
    return ec ? src.string() : dst.string();
}

AmpDefinition& AmpLibrary::getOrCreate (const std::string& make, const std::string& model, const std::string& key)
{
    for (auto& a : amps) if (a.identityKey == key) return a;
    AmpDefinition a;
    a.id = makeId();
    a.manufacturer = make;
    a.model = model;
    a.identityKey = key;
    a.name = make.empty() ? model : make + " " + model;
    amps.push_back (std::move (a));
    return amps.back();
}

ImportResult AmpLibrary::importFiles (const std::vector<fs::path>& files, Progress progress, bool analyse)
{
    ImportResult res;
    const auto all = NamLoader::collectNamFiles (files);
    std::set<std::string> touched;

    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto& f = all[i];
        if (progress && ! progress ((int) i, (int) all.size(), f.filename().string())) break;
        try
        {
            CaptureEntry entry;
            entry.id = makeId();
            entry.meta = readCaptureMetadata (f);
            entry.coords = entry.meta.knobs;

            if (analyse)
            {
                NamModel m;
                m.load (f);
                entry.analysis = ProfileAnalyzer::analyse (m);
                if (entry.meta.expectedSampleRate <= 0) entry.meta.expectedSampleRate = m.getModelSampleRate();
            }

            if (! entry.meta.hasIdentity())
            {
                res.pending.push_back ({ f.string(), entry.meta, entry.analysis });
                continue;
            }

            const auto key = entry.meta.identityKey();
            AmpDefinition& amp = getOrCreate (entry.meta.manufacturer, entry.meta.model, key);

            // duplicate = same original file name + same size already in this amp
            bool dup = false;
            std::error_code ec;
            const auto sz = fs::file_size (f, ec);
            for (auto& ch : amp.channels)
                for (auto& c : ch.captures)
                    if (c.meta.fileName == entry.meta.fileName && fs::file_size (c.path, ec) == sz) dup = true;
            if (dup) { ++res.duplicates; continue; }

            entry.path = placeInLibrary (amp.id, f);
            ChannelManager::addCapture (amp, std::move (entry));
            touched.insert (amp.id);
            ++res.imported;
        }
        catch (const std::exception& e)
        {
            res.errors.push_back (f.filename().string() + ": " + e.what());
        }
    }

    for (auto& a : amps)
        if (touched.count (a.id))
        {
            ChannelManager::sortChannels (a);
            for (auto& ch : a.channels) ChannelManager::refreshEstimates (ch);
            writeAmp (a);
            res.ampIds.push_back (a.id);
        }
    if (progress) progress ((int) all.size(), (int) all.size(), {});
    return res;
}

std::string AmpLibrary::createBlend (const std::string& name, const std::vector<fs::path>& files, std::string& error)
{
    const auto all = NamLoader::collectNamFiles (files);
    if (all.empty()) { error = "Pick at least one NAM model."; return {}; }
    if ((int) all.size() > AmpDefinition::kMaxBlendModels)
    { error = "A blend holds up to " + std::to_string (AmpDefinition::kMaxBlendModels) + " models."; return {}; }

    AmpDefinition amp;
    amp.id = makeId();
    amp.isBlend = true;
    amp.name = name.empty() ? std::string ("Blend") : name;
    amp.manufacturer = "Model blend";
    amp.model = amp.name;
    amp.identityKey = "blend|" + amp.id; // unique: automatic imports never join a blend
    ChannelDefinition ch;
    ch.name = "Blend";
    for (const auto& f : all)
    {
        try
        {
            CaptureEntry e;
            e.id = makeId();
            e.meta = readCaptureMetadata (f);
            NamModel m;
            m.load (f);
            e.analysis = ProfileAnalyzer::analyse (m);
            if (e.meta.expectedSampleRate <= 0) e.meta.expectedSampleRate = m.getModelSampleRate();
            e.path = placeInLibrary (amp.id, f);
            e.blendSlot = (int) ch.captures.size();
            ch.captures.push_back (std::move (e));
        }
        catch (const std::exception& ex) { error = f.filename().string() + ": " + ex.what(); return {}; }
    }
    amp.channels.push_back (std::move (ch));
    writeAmp (amp);
    amps.push_back (amp);
    return amp.id;
}

std::string AmpLibrary::setBlendSlot (const std::string& ampId, int slot, const fs::path& file,
                                      const std::string& newBlendName, std::string& error)
{
    if (slot < 0 || slot >= AmpDefinition::kMaxBlendModels) { error = "No such slot."; return {}; }
    AmpDefinition* amp = nullptr;
    for (auto& a : amps) if (a.id == ampId && a.isBlend) amp = &a;
    if (amp == nullptr)
    {
        AmpDefinition a;
        a.id = makeId();
        a.isBlend = true;
        a.name = newBlendName.empty() ? std::string ("New Blend") : newBlendName;
        a.manufacturer = "Model blend";
        a.model = a.name;
        a.identityKey = "blend|" + a.id;
        ChannelDefinition ch;
        ch.name = "Blend";
        a.channels.push_back (ch);
        amps.push_back (a);
        amp = &amps.back();
    }
    if (amp->channels.empty()) { ChannelDefinition ch; ch.name = "Blend"; amp->channels.push_back (ch); }

    CaptureEntry e;
    try
    {
        e.id = makeId();
        e.meta = readCaptureMetadata (file);
        NamModel m;
        m.load (file);
        e.analysis = ProfileAnalyzer::analyse (m);
        if (e.meta.expectedSampleRate <= 0) e.meta.expectedSampleRate = m.getModelSampleRate();
        e.path = placeInLibrary (amp->id, file);
        e.blendSlot = slot;
    }
    catch (const std::exception& ex) { error = file.filename().string() + ": " + ex.what(); return {}; }

    auto& caps = amp->channels[0].captures;
    // legacy blends: give every capture an explicit slot before replacing
    for (size_t i = 0; i < caps.size(); ++i) if (caps[i].blendSlot < 0) caps[i].blendSlot = (int) i;
    caps.erase (std::remove_if (caps.begin(), caps.end(), [slot] (const CaptureEntry& c) { return c.blendSlot == slot; }), caps.end());
    caps.push_back (std::move (e));
    std::sort (caps.begin(), caps.end(), [] (const CaptureEntry& a, const CaptureEntry& b) { return a.blendSlot < b.blendSlot; });
    writeAmp (*amp);
    return amp->id;
}

bool AmpLibrary::clearBlendSlot (const std::string& ampId, int slot)
{
    for (auto& a : amps)
        if (a.id == ampId && a.isBlend && ! a.channels.empty())
        {
            auto& caps = a.channels[0].captures;
            for (size_t i = 0; i < caps.size(); ++i) if (caps[i].blendSlot < 0) caps[i].blendSlot = (int) i;
            const auto before = caps.size();
            caps.erase (std::remove_if (caps.begin(), caps.end(), [slot] (const CaptureEntry& c) { return c.blendSlot == slot; }), caps.end());
            if (caps.size() != before) { writeAmp (a); return true; }
        }
    return false;
}

std::string AmpLibrary::assignPending (const PendingCapture& p, const std::string& make, const std::string& model,
                                       const std::string& channel)
{
    CaptureEntry entry;
    entry.id = makeId();
    entry.meta = p.meta;
    entry.meta.manufacturer = make;
    entry.meta.model = model;
    entry.coords = p.meta.knobs;
    entry.analysis = p.analysis;

    const auto key = entry.meta.identityKey();
    AmpDefinition& amp = getOrCreate (make, model, key);
    entry.path = placeInLibrary (amp.id, p.path);
    ChannelManager::addCapture (amp, std::move (entry), channel);
    ChannelManager::sortChannels (amp);
    for (auto& ch : amp.channels) ChannelManager::refreshEstimates (ch);
    writeAmp (amp);
    return amp.id;
}
} // namespace namknobs
