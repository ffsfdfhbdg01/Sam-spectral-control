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

/** Short Dolby-style label of a role (L, R, C, LFE, Lss, Rss, Lrs, Rrs, Ltf, …); empty for Unknown. */
const char* speakerLabel (SpeakerRole role) noexcept;

/**
    The loudspeakers a band works on (research §4c, CH-4). Stored by role, not by channel index,
    so a preset moves between layouts: "All" from stereo covers every speaker of 7.1.4. Channels
    without a role (discrete layouts) are selected by index. The default is every speaker but
    LFE/LFE2 (as in Pro-Q 4).
*/
constexpr std::uint64_t speakerBit (SpeakerRole role) noexcept
{
    return std::uint64_t { 1 } << static_cast<int> (role);
}

struct SpeakerSelection
{
    static constexpr std::uint64_t kLfeRoles = speakerBit (SpeakerRole::Lfe) | speakerBit (SpeakerRole::Lfe2);
    static constexpr std::uint64_t kAllRoles = ~std::uint64_t { 0 };
    static constexpr std::uint64_t kDefaultRoles = kAllRoles & ~kLfeRoles;
    static constexpr std::uint32_t kAllDiscrete = ~std::uint32_t { 0 };

    std::uint64_t roles = kDefaultRoles;
    std::uint32_t discrete = kAllDiscrete; ///< by channel index, for channels without a role

    bool operator== (const SpeakerSelection&) const = default;

    bool isDefault() const noexcept { return *this == SpeakerSelection {}; }
    bool includesLfe() const noexcept { return (roles & kLfeRoles) != 0; }

    /** True if channel ch of the topology is selected. */
    bool selects (const ChannelTopology& topology, int ch) const noexcept;

    /** Mask of the selected channel indices of a topology (BandSettings::channelMask). */
    std::uint64_t channelMask (const ChannelTopology& topology) const noexcept;

    /** Number of channels of the topology that are selected. */
    int count (const ChannelTopology& topology) const noexcept;
};
} // namespace ssc::engine
