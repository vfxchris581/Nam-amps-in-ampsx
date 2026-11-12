#include "ChannelManager.h"

#include <algorithm>

namespace namified
{
int ChannelManager::addCapture (AmpDefinition& amp, CaptureEntry capture, const std::string& forcedChannel)
{
    std::string chName = ! forcedChannel.empty() ? forcedChannel : capture.meta.channelHint;
    if (chName.empty())
        chName = amp.channels.size() == 1 ? amp.channels[0].name : std::string (kDefaultChannel);

    for (int c = 0; c < kNumControls; ++c)
        if (capture.source[(size_t) c] == CoordSource::Unknown && isKnown (capture.coords[(size_t) c]))
            capture.source[(size_t) c] = CoordSource::Detected;

    int idx = amp.findChannel (chName);
    if (idx < 0)
    {
        // A lone default channel adopts the first real channel name instead of becoming a 2nd channel.
        if (amp.channels.size() == 1 && amp.channels[0].name == kDefaultChannel && ! capture.meta.channelHint.empty()
            && std::all_of (amp.channels[0].captures.begin(), amp.channels[0].captures.end(),
                            [] (const CaptureEntry& e) { return e.meta.channelHint.empty(); })
            && amp.channels[0].captures.empty())
        {
            amp.channels[0].name = chName;
            idx = 0;
        }
        else
        {
            if ((int) amp.channels.size() >= kMaxChannels) idx = (int) amp.channels.size() - 1;
            else { idx = addChannel (amp, chName); }
        }
    }
    amp.channels[(size_t) idx].captures.push_back (std::move (capture));
    return idx;
}

int ChannelManager::channelRank (const std::string& name)
{
    const auto n = normaliseName (name);
    if (n.rfind ("channel ", 0) == 0) return 10 + std::atoi (n.c_str() + 8);
    static const char* order[] = { "main", "clean", "normal", "vintage", "classic", "plexi", "crunch", "rhythm",
                                   "green", "orange", "raw", "overdrive", "lead", "red", "modern", "solo", "ultra",
                                   "boost" };
    for (int i = 0; i < (int) (sizeof (order) / sizeof (order[0])); ++i)
        if (n == order[i]) return 100 + i;
    return 1000;
}

void ChannelManager::sortChannels (AmpDefinition& amp)
{
    std::stable_sort (amp.channels.begin(), amp.channels.end(),
                      [] (const ChannelDefinition& a, const ChannelDefinition& b) { return channelRank (a.name) < channelRank (b.name); });
}

bool ChannelManager::moveCapture (AmpDefinition& amp, int from, int capIdx, int to)
{
    if (from < 0 || to < 0 || from >= (int) amp.channels.size() || to >= (int) amp.channels.size() || from == to) return false;
    auto& src = amp.channels[(size_t) from].captures;
    if (capIdx < 0 || capIdx >= (int) src.size()) return false;
    amp.channels[(size_t) to].captures.push_back (std::move (src[(size_t) capIdx]));
    src.erase (src.begin() + capIdx);
    return true;
}

bool ChannelManager::renameChannel (AmpDefinition& amp, int channel, const std::string& newName)
{
    if (channel < 0 || channel >= (int) amp.channels.size() || newName.empty()) return false;
    const int existing = amp.findChannel (newName);
    if (existing >= 0 && existing != channel) return false;
    amp.channels[(size_t) channel].name = newName;
    return true;
}

int ChannelManager::addChannel (AmpDefinition& amp, const std::string& name)
{
    if ((int) amp.channels.size() >= kMaxChannels) return -1;
    if (amp.findChannel (name) >= 0) return amp.findChannel (name);
    ChannelDefinition ch;
    ch.name = name;
    amp.channels.push_back (ch);
    return (int) amp.channels.size() - 1;
}

bool ChannelManager::removeChannel (AmpDefinition& amp, int channel)
{
    if (amp.channels.size() <= 1 || channel < 0 || channel >= (int) amp.channels.size()) return false;
    auto caps = std::move (amp.channels[(size_t) channel].captures);
    amp.channels.erase (amp.channels.begin() + channel);
    for (auto& c : caps) amp.channels[0].captures.push_back (std::move (c));
    return true;
}

void ChannelManager::pruneEmpty (AmpDefinition& amp)
{
    for (int i = (int) amp.channels.size() - 1; i >= 0 && amp.channels.size() > 1; --i)
        if (amp.channels[(size_t) i].captures.empty()) amp.channels.erase (amp.channels.begin() + i);
}

void ChannelManager::refreshEstimates (ChannelDefinition& ch)
{
    // Clear previous estimates (keep detected + user values), then re-estimate.
    std::vector<KnobCoords> coords;
    std::vector<CaptureAnalysis> analyses;
    for (auto& c : ch.captures)
    {
        for (int d = 0; d < kNumControls; ++d)
            if (c.source[(size_t) d] == CoordSource::Estimated) { c.coords[(size_t) d] = std::nan (""); c.source[(size_t) d] = CoordSource::Unknown; }
        coords.push_back (c.coords);
        analyses.push_back (c.analysis ? *c.analysis : CaptureAnalysis {});
    }
    const auto flags = ProfileAnalyzer::estimateCoordinates (coords, analyses);
    for (size_t i = 0; i < ch.captures.size(); ++i)
        for (int d = 0; d < kNumControls; ++d)
            if (flags[i][(size_t) d])
            {
                ch.captures[i].coords[(size_t) d] = coords[i][(size_t) d];
                ch.captures[i].source[(size_t) d] = CoordSource::Estimated;
            }
}
} // namespace namified
