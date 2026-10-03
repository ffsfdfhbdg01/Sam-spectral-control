#include "ssc/engine/ChannelTopology.h"

#include <algorithm>
#include <utility>

namespace ssc::engine
{
namespace
{
    constexpr std::pair<SpeakerRole, SpeakerRole> kPairs[] = {
        { SpeakerRole::Left, SpeakerRole::Right },
        { SpeakerRole::LeftSurround, SpeakerRole::RightSurround },
        { SpeakerRole::LeftSurroundSide, SpeakerRole::RightSurroundSide },
        { SpeakerRole::LeftSurroundRear, SpeakerRole::RightSurroundRear },
        { SpeakerRole::LeftCentre, SpeakerRole::RightCentre },
        { SpeakerRole::LeftWide, SpeakerRole::RightWide },
        { SpeakerRole::TopFrontLeft, SpeakerRole::TopFrontRight },
        { SpeakerRole::TopMiddleLeft, SpeakerRole::TopMiddleRight },
        { SpeakerRole::TopRearLeft, SpeakerRole::TopRearRight },
        { SpeakerRole::BottomFrontLeft, SpeakerRole::BottomFrontRight },
        { SpeakerRole::ProximityLeft, SpeakerRole::ProximityRight },
        { SpeakerRole::BottomSideLeft, SpeakerRole::BottomSideRight },
        { SpeakerRole::BottomRearLeft, SpeakerRole::BottomRearRight },
    };

    int find (const ChannelTopology& t, SpeakerRole role) noexcept
    {
        for (int ch = 0; ch < t.numChannels; ++ch)
            if (t.roles[static_cast<std::size_t> (ch)] == role)
                return ch;
        return -1;
    }
} // namespace

ChannelTopology ChannelTopology::fromRoles (const SpeakerRole* roles, int numChannels) noexcept
{
    ChannelTopology t;
    t.numChannels = std::clamp (numChannels, 0, kMaxChannels);
    std::copy_n (roles, t.numChannels, t.roles.begin());
    t.pairIndex.fill (-1);

    for (const auto& [leftRole, rightRole] : kPairs)
    {
        const int l = find (t, leftRole);
        const int r = find (t, rightRole);
        if (l < 0 || r < 0)
            continue;

        const int index = t.numPairs++;
        t.pairs[static_cast<std::size_t> (index)] = { l, r };
        t.pairIndex[static_cast<std::size_t> (l)] = index;
        t.pairIndex[static_cast<std::size_t> (r)] = index;
    }

    return t;
}

ChannelTopology ChannelTopology::stereo() noexcept
{
    const SpeakerRole roles[] = { SpeakerRole::Left, SpeakerRole::Right };
    return fromRoles (roles, 2);
}

ChannelTopology ChannelTopology::mono() noexcept
{
    const SpeakerRole roles[] = { SpeakerRole::Mono };
    return fromRoles (roles, 1);
}

ChannelTopology ChannelTopology::discrete (int numChannels) noexcept
{
    std::array<SpeakerRole, kMaxChannels> roles {};
    roles.fill (SpeakerRole::Unknown);
    return fromRoles (roles.data(), numChannels);
}
} // namespace ssc::engine
