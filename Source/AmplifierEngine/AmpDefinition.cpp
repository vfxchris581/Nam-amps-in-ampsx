#include "AmpDefinition.h"

#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace namknobs
{
int AmpDefinition::totalCaptures() const
{
    int n = 0;
    for (auto& c : channels) n += (int) c.captures.size();
    return n;
}

int AmpDefinition::findChannel (const std::string& n) const
{
    const auto key = normaliseName (n);
    for (size_t i = 0; i < channels.size(); ++i)
        if (normaliseName (channels[i].name) == key) return (int) i;
    return -1;
}

std::string makeId()
{
    static std::mt19937_64 rng ((uint64_t) std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::ostringstream s;
    s << std::hex << std::setw (16) << std::setfill ('0') << rng();
    return s.str();
}

namespace
{
nlohmann::json coordsJson (const KnobCoords& k)
{
    auto a = nlohmann::json::array();
    for (double v : k) a.push_back (isKnown (v) ? nlohmann::json (v) : nlohmann::json (nullptr));
    return a;
}
KnobCoords coordsFrom (const nlohmann::json& j, double fallback)
{
    KnobCoords k;
    k.fill (fallback);
    if (! j.is_array()) return k;
    for (size_t i = 0; i < k.size() && i < j.size(); ++i)
        k[i] = j[i].is_number() ? j[i].get<double>() : std::nan ("");
    return k;
}
template <typename T> T get (const nlohmann::json& j, const char* key, T def)
{
    if (j.contains (key) && ! j[key].is_null())
    {
        try { return j[key].get<T>(); } catch (...) {}
    }
    return def;
}
} // namespace

nlohmann::json toJson (const AmpDefinition& a)
{
    nlohmann::json j;
    j["format"] = "namknobs-amp";
    j["version"] = 1;
    j["id"] = a.id;
    j["name"] = a.name;
    j["manufacturer"] = a.manufacturer;
    j["model"] = a.model;
    j["identityKey"] = a.identityKey;
    j["ir1"] = a.ir1Path;
    j["ir2"] = a.ir2Path;
    j["notes"] = a.notes;
    j["blend"] = a.isBlend;
    auto chans = nlohmann::json::array();
    for (auto& ch : a.channels)
    {
        nlohmann::json cj;
        cj["name"] = ch.name;
        cj["savedKnobs"] = coordsJson (ch.savedKnobs);
        auto caps = nlohmann::json::array();
        for (auto& c : ch.captures)
        {
            nlohmann::json e;
            e["id"] = c.id;
            e["path"] = c.path;
            e["name"] = c.meta.name;
            e["fileName"] = c.meta.fileName;
            e["manufacturer"] = c.meta.manufacturer;
            e["model"] = c.meta.model;
            e["toneType"] = c.meta.toneType;
            e["channelHint"] = c.meta.channelHint;
            e["architecture"] = c.meta.architecture;
            e["sampleRate"] = c.meta.expectedSampleRate;
            if (c.meta.loudnessDb) e["loudness"] = *c.meta.loudnessDb;
            e["coords"] = coordsJson (c.coords);
            auto src = nlohmann::json::array();
            for (auto s : c.source) src.push_back ((int) s);
            e["coordSource"] = src;
            e["trimDb"] = c.trimDb;
            e["blendSlot"] = c.blendSlot;
            e["enabled"] = c.enabled;
            if (c.analysis && c.analysis->valid)
            {
                const auto& an = *c.analysis;
                e["analysis"] = { { "sampleRate", an.sampleRate }, { "latency", an.latencySamples },
                                  { "polarity", an.polarity }, { "loudness", an.loudnessDb },
                                  { "compression", an.compression }, { "thd", an.thdDb },
                                  { "evenOdd", an.evenOddDb },
                                  { "bands", std::vector<double> (an.bandsDb.begin(), an.bandsDb.end()) } };
            }
            caps.push_back (e);
        }
        cj["captures"] = caps;
        chans.push_back (cj);
    }
    j["channels"] = chans;
    return j;
}

AmpDefinition ampFromJson (const nlohmann::json& j)
{
    AmpDefinition a;
    a.id = get<std::string> (j, "id", makeId());
    a.name = get<std::string> (j, "name", "Amp");
    a.manufacturer = get<std::string> (j, "manufacturer", "");
    a.model = get<std::string> (j, "model", "");
    a.identityKey = get<std::string> (j, "identityKey", "");
    a.ir1Path = get<std::string> (j, "ir1", "");
    a.ir2Path = get<std::string> (j, "ir2", "");
    a.notes = get<std::string> (j, "notes", "");
    a.isBlend = get<bool> (j, "blend", false);
    if (j.contains ("channels") && j["channels"].is_array())
        for (auto& cj : j["channels"])
        {
            ChannelDefinition ch;
            ch.name = get<std::string> (cj, "name", "Channel");
            if (cj.contains ("savedKnobs")) ch.savedKnobs = coordsFrom (cj["savedKnobs"], 5.0);
            for (auto& k : ch.savedKnobs) if (! isKnown (k)) k = 5.0;
            if (cj.contains ("captures") && cj["captures"].is_array())
                for (auto& e : cj["captures"])
                {
                    CaptureEntry c;
                    c.id = get<std::string> (e, "id", makeId());
                    c.path = get<std::string> (e, "path", "");
                    c.meta.name = get<std::string> (e, "name", "");
                    c.meta.fileName = get<std::string> (e, "fileName", "");
                    c.meta.manufacturer = get<std::string> (e, "manufacturer", "");
                    c.meta.model = get<std::string> (e, "model", "");
                    c.meta.toneType = get<std::string> (e, "toneType", "");
                    c.meta.channelHint = get<std::string> (e, "channelHint", "");
                    c.meta.architecture = get<std::string> (e, "architecture", "");
                    c.meta.expectedSampleRate = get<double> (e, "sampleRate", -1.0);
                    if (e.contains ("loudness") && e["loudness"].is_number()) c.meta.loudnessDb = e["loudness"].get<double>();
                    c.coords = coordsFrom (e.value ("coords", nlohmann::json()), std::nan (""));
                    if (e.contains ("coordSource") && e["coordSource"].is_array())
                        for (size_t i = 0; i < c.source.size() && i < e["coordSource"].size(); ++i)
                            c.source[i] = (CoordSource) e["coordSource"][i].get<int>();
                    c.trimDb = get<double> (e, "trimDb", 0.0);
                    c.blendSlot = get<int> (e, "blendSlot", -1);
                    c.enabled = get<bool> (e, "enabled", true);
                    if (e.contains ("analysis") && e["analysis"].is_object())
                    {
                        const auto& aj = e["analysis"];
                        CaptureAnalysis an;
                        an.valid = true;
                        an.sampleRate = get<double> (aj, "sampleRate", 48000.0);
                        an.latencySamples = get<int> (aj, "latency", 0);
                        an.polarity = get<int> (aj, "polarity", 1);
                        an.loudnessDb = get<double> (aj, "loudness", -18.0);
                        an.compression = get<double> (aj, "compression", 1.0);
                        an.thdDb = get<double> (aj, "thd", -60.0);
                        an.evenOddDb = get<double> (aj, "evenOdd", 0.0);
                        auto bands = get<std::vector<double>> (aj, "bands", {});
                        for (size_t i = 0; i < an.bandsDb.size() && i < bands.size(); ++i) an.bandsDb[i] = bands[i];
                        c.analysis = an;
                    }
                    ch.captures.push_back (std::move (c));
                }
            a.channels.push_back (std::move (ch));
        }
    return a;
}
} // namespace namknobs
