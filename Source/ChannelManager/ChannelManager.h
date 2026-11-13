#pragma once
// Builds and edits the channel layout of a virtual amp.
// Channels come from explicit evidence only (a "ch2"/"lead"/... token in the capture's name or
// metadata, or the user's choice). Captures without a channel hint join the amp's single default
// channel, so a one-channel amp stays one channel.

#include <string>
#include "AmplifierEngine/AmpDefinition.h"

namespace namknobs
{
class ChannelManager
{
public:
    static constexpr int kMaxChannels = 6;
    static constexpr const char* kDefaultChannel = "Main";

    /** Adds a capture to the channel its hint names (creating the channel when needed).
        Returns the channel index. */
    static int addCapture (AmpDefinition& amp, CaptureEntry capture, const std::string& forcedChannel = {});

    /** Deterministic channel order: numbered channels, then clean → crunch → rhythm → lead → others. */
    static void sortChannels (AmpDefinition& amp);

    static bool moveCapture (AmpDefinition& amp, int fromChannel, int captureIndex, int toChannel);
    static bool renameChannel (AmpDefinition& amp, int channel, const std::string& newName);
    static int addChannel (AmpDefinition& amp, const std::string& name);
    /** Removes a channel; its captures move to the first remaining channel. */
    static bool removeChannel (AmpDefinition& amp, int channel);
    /** Drops channels that ended up with no captures (keeps at least one). */
    static void pruneEmpty (AmpDefinition& amp);

    /** Recomputes the coordinate source flags + fills estimable coordinates from analyses. */
    static void refreshEstimates (ChannelDefinition& ch);

private:
    static int channelRank (const std::string& name);
};
} // namespace namknobs
