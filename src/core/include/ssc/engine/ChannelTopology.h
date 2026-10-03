#pragma once

#include <array>
#include <cstdint>

#include "ssc/engine/Types.h"

namespace ssc::engine
{
/** Semantic loudspeaker roles (Dolby naming in comments). Append only — values may be saved. */
enum class SpeakerRole : std::uint8_t
{
    Unknown,
    Mono,
    Left,              // L
    Right,             // R
    Centre,            // C
    Lfe,               // LFE
    Lfe2,              // LFE2
    LeftSurround,      // Ls (5.x)
    RightSurround,     // Rs (5.x)
    LeftSurroundSide,  // Lss
    RightSurroundSide, // Rss
    LeftSurroundRear,  // Lrs
    RightSurroundRear, // Rrs
    CentreSurround,    // Cs
    LeftCentre,        // Lc
    RightCentre,       // Rc
    LeftWide,          // Lw
    RightWide,         // Rw
    TopFrontLeft,      // Ltf
    TopFrontRight,     // Rtf
    TopFrontCentre,    // Ctf
    TopMiddleLeft,     // Ltm
    TopMiddleRight,    // Rtm
    TopMiddleCentre,   // Tc
    TopRearLeft,       // Ltr
    TopRearRight,      // Rtr
    TopRearCentre,     // Ctr
    BottomFrontLeft,
    BottomFrontRight,
    BottomFrontCentre,
    ProximityLeft,
    ProximityRight,
    BottomSideLeft,
    BottomSideRight,
    BottomRearLeft,
    BottomRearRight,
    BottomRearCentre
};

/** Loudspeaker groups used for detector linking (research §4). */
enum class SpeakerGroup : std::uint8_t
{
    Front,
    Centre,
    Lfe,
    SideSurround,
    RearSurround,
    Wides,
    TopFront,
    TopMiddle,
    TopRear,
    Other
};

SpeakerGroup groupOf (SpeakerRole role) noexcept;

/**
    Channel layout as seen by the engine: roles, symmetric left/right pairs and LFE flags (research §4).
    Built on the message thread from the host bus layout; read-only on the audio thread.
*/
struct ChannelTopology
{
    struct Pair
    {
        int left = -1;
        int right = -1;
    };

    int numChannels = 0;
    std::array<SpeakerRole, kMaxChannels> roles {};
    std::array<Pair, kMaxChannels / 2> pairs {};
    int numPairs = 0;
    std::array<int, kMaxChannels> pairIndex {}; ///< pair containing the channel, or −1 for singles
    std::array<SpeakerGroup, kMaxChannels> groups {};

    bool isLfe (int channel) const noexcept
    {
        const auto r = roles[static_cast<std::size_t> (channel)];
        return r == SpeakerRole::Lfe || r == SpeakerRole::Lfe2;
    }

    static ChannelTopology fromRoles (const SpeakerRole* roles, int numChannels) noexcept;
    static ChannelTopology stereo() noexcept;
    static ChannelTopology mono() noexcept;
    static ChannelTopology discrete (int numChannels) noexcept;
};
} // namespace ssc::engine
