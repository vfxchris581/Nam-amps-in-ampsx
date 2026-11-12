#include "CaptureMetadata.h"

#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <json.hpp>

namespace namified
{
const char* controlName (Control c) noexcept
{
    switch (c)
    {
        case Control::Gain: return "Gain";
        case Control::Volume: return "Volume";
        case Control::Bass: return "Bass";
        case Control::Middle: return "Middle";
        case Control::Treble: return "Treble";
        case Control::Presence: return "Presence";
        case Control::Resonance: return "Resonance";
        case Control::Master: return "Master";
        default: return "?";
    }
}

const char* controlId (Control c) noexcept
{
    switch (c)
    {
        case Control::Gain: return "gain";
        case Control::Volume: return "volume";
        case Control::Bass: return "bass";
        case Control::Middle: return "middle";
        case Control::Treble: return "treble";
        case Control::Presence: return "presence";
        case Control::Resonance: return "resonance";
        case Control::Master: return "master";
        default: return "?";
    }
}

std::string normaliseName (const std::string& s)
{
    std::string out;
    bool space = false;
    for (unsigned char ch : s)
    {
        if (std::isalnum (ch)) { if (space && ! out.empty()) out += ' '; out += (char) std::tolower (ch); space = false; }
        else space = true;
    }
    return out;
}

std::string CaptureMetadata::identityKey() const
{
    if (! hasIdentity()) return {};
    return normaliseName (manufacturer) + "|" + normaliseName (model);
}

namespace
{
struct Atom { std::string text; bool isNumber; size_t begin, end; };

std::vector<Atom> atomise (const std::string& s)
{
    std::vector<Atom> atoms;
    size_t i = 0;
    while (i < s.size())
    {
        const unsigned char c = (unsigned char) s[i];
        if (std::isalpha (c))
        {
            const size_t b = i;
            std::string t;
            while (i < s.size() && std::isalpha ((unsigned char) s[i])) t += (char) std::tolower ((unsigned char) s[i++]);
            atoms.push_back ({ t, false, b, i });
        }
        else if (std::isdigit (c))
        {
            const size_t b = i;
            std::string t;
            bool dot = false;
            while (i < s.size() && (std::isdigit ((unsigned char) s[i])
                                    || (! dot && (s[i] == '.' || s[i] == ',') && i + 1 < s.size()
                                        && std::isdigit ((unsigned char) s[i + 1]))))
            {
                if (s[i] == '.' || s[i] == ',') { dot = true; t += '.'; ++i; }
                else t += s[i++];
            }
            atoms.push_back ({ t, true, b, i });
        }
        else ++i;
    }
    return atoms;
}

const std::map<std::string, Control>& knobAliases()
{
    static const std::map<std::string, Control> m {
        { "gain", Control::Gain }, { "g", Control::Gain }, { "gn", Control::Gain }, { "drive", Control::Gain },
        { "drv", Control::Gain }, { "pregain", Control::Gain },
        { "volume", Control::Volume }, { "vol", Control::Volume }, { "chvol", Control::Volume },
        { "bass", Control::Bass }, { "b", Control::Bass }, { "bas", Control::Bass }, { "low", Control::Bass }, { "lo", Control::Bass },
        { "middle", Control::Middle }, { "mid", Control::Middle }, { "mids", Control::Middle }, { "m", Control::Middle },
        { "treble", Control::Treble }, { "treb", Control::Treble }, { "tre", Control::Treble }, { "t", Control::Treble },
        { "presence", Control::Presence }, { "pres", Control::Presence }, { "prs", Control::Presence }, { "p", Control::Presence },
        { "resonance", Control::Resonance }, { "res", Control::Resonance }, { "reso", Control::Resonance }, { "r", Control::Resonance },
        { "depth", Control::Resonance }, { "dep", Control::Resonance }, { "density", Control::Resonance },
        { "dens", Control::Resonance }, { "thump", Control::Resonance },
        { "master", Control::Master }, { "mv", Control::Master }, { "mstr", Control::Master }, { "mvol", Control::Master },
        { "mas", Control::Master },
    };
    return m;
}

const std::set<std::string>& channelWords()
{
    static const std::set<std::string> s { "clean", "crunch", "lead", "rhythm", "overdrive", "modern", "vintage",
                                           "raw", "ultra", "red", "orange", "green", "blue", "yellow", "purple",
                                           "normal", "bright", "solo", "classic", "boost" };
    return s;
}

const std::set<std::string>& noiseWords()
{
    static const std::set<std::string> s { "nam", "capture", "captures", "profile", "di", "amp", "head", "combo",
                                           "model", "standard", "feather", "lite", "nano", "wavenet", "lstm",
                                           "direct", "loadbox", "full", "rig", "preamp", "cab", "no", "ir",
                                           "settings", "setting", "tone", "v", "ch", "chan", "channel", "mode" };
    return s;
}

double scaleKnob (double v)
{
    if (v > 10.0 && v <= 100.0) return v / 10.0;
    return v;
}

std::string stringOr (const nlohmann::json& j, const char* key)
{
    if (j.contains (key) && j[key].is_string()) return j[key].get<std::string>();
    return {};
}

std::optional<double> numberOr (const nlohmann::json& j, const char* key)
{
    if (j.contains (key) && j[key].is_number()) return j[key].get<double>();
    return std::nullopt;
}
} // namespace

void parseSettingsFromText (const std::string& text, CaptureMetadata& meta)
{
    const auto atoms = atomise (text);
    const auto& aliases = knobAliases();

    for (size_t i = 0; i < atoms.size(); ++i)
    {
        const Atom& a = atoms[i];
        if (a.isNumber) continue;

        const bool nextIsNumber = i + 1 < atoms.size() && atoms[i + 1].isNumber
                                  && atoms[i + 1].begin - a.end <= 3; // "G7", "Gain 7", "gain=7", "gain_7"

        if ((a.text == "ch" || a.text == "chan" || a.text == "channel") && nextIsNumber)
        {
            const int n = (int) std::stod (atoms[i + 1].text);
            if (n >= 1 && n <= 8) { meta.channelNumberHint = n; meta.channelHint = "Channel " + std::to_string (n); }
            ++i;
            continue;
        }

        if (nextIsNumber)
        {
            auto it = aliases.find (a.text);
            if (it != aliases.end())
            {
                const double v = scaleKnob (std::stod (atoms[i + 1].text));
                if (v >= 0.0 && v <= 10.0)
                    meta.knobs[(size_t) it->second] = v;
                ++i;
                continue;
            }
        }

        if (meta.channelHint.empty() && channelWords().count (a.text) > 0
            && ! (nextIsNumber && aliases.count (a.text) > 0))
        {
            std::string word = a.text;
            word[0] = (char) std::toupper ((unsigned char) word[0]);
            meta.channelHint = word;
        }
    }
}

void guessIdentityFromText (const std::string& text, CaptureMetadata& meta)
{
    const auto atoms = atomise (text);
    const auto& aliases = knobAliases();
    std::vector<std::string> words;
    for (size_t i = 0; i < atoms.size(); ++i)
    {
        const Atom& a = atoms[i];
        const bool nextIsNumber = i + 1 < atoms.size() && atoms[i + 1].isNumber && atoms[i + 1].begin - a.end <= 3;
        if (! a.isNumber && nextIsNumber && (aliases.count (a.text) || a.text == "ch" || a.text == "chan" || a.text == "channel"))
        { ++i; continue; }
        if (! a.isNumber && (channelWords().count (a.text) || noiseWords().count (a.text))) continue;
        // keep model numbers like "5150", "800", but keep original casing from the text
        words.push_back (text.substr (a.begin, a.end - a.begin));
    }
    if (words.empty()) return;
    meta.suggestedManufacturer = words.front();
    std::string rest;
    for (size_t i = 1; i < words.size() && i < 5; ++i) rest += (rest.empty() ? "" : " ") + words[i];
    meta.suggestedModel = rest.empty() ? words.front() : rest;
}

CaptureMetadata readCaptureMetadata (const std::filesystem::path& file)
{
    std::ifstream in (file, std::ios::binary);
    if (! in) throw std::runtime_error ("Cannot open " + file.string());

    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) { throw std::runtime_error ("Not a valid .nam (JSON) file: " + file.filename().string()); }

    if (! j.is_object() || ! j.contains ("architecture"))
        throw std::runtime_error ("Not a NAM model file: " + file.filename().string());

    CaptureMetadata m;
    m.fileName = file.stem().string();
    m.architecture = stringOr (j, "architecture");
    m.napVersion = stringOr (j, "version");
    if (auto sr = numberOr (j, "sample_rate")) m.expectedSampleRate = *sr;

    // SlimmableContainer / Sequential files nest the metadata in their first submodel.
    const nlohmann::json* metaJson = nullptr;
    if (j.contains ("metadata") && j["metadata"].is_object()) metaJson = &j["metadata"];
    else if (j.contains ("config") && j["config"].contains ("submodels") && ! j["config"]["submodels"].empty())
    {
        const auto& sub = j["config"]["submodels"][0];
        if (sub.contains ("model") && sub["model"].contains ("metadata")) metaJson = &sub["model"]["metadata"];
    }

    if (metaJson != nullptr)
    {
        const auto& md = *metaJson;
        m.name = stringOr (md, "name");
        m.manufacturer = stringOr (md, "gear_make");
        m.model = stringOr (md, "gear_model");
        m.gearType = stringOr (md, "gear_type");
        m.toneType = stringOr (md, "tone_type");
        m.modeledBy = stringOr (md, "modeled_by");
        m.loudnessDb = numberOr (md, "loudness");
        m.inputLevelDbu = numberOr (md, "input_level_dbu");
        m.outputLevelDbu = numberOr (md, "output_level_dbu");
    }
    if (m.name.empty()) m.name = m.fileName;
    m.identityFromMetadata = m.hasIdentity();

    // Settings: file name first, then the metadata name (more specific wins).
    parseSettingsFromText (m.fileName, m);
    if (m.name != m.fileName) parseSettingsFromText (m.name, m);

    if (! m.identityFromMetadata) guessIdentityFromText (m.name, m);
    return m;
}
} // namespace namified
