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

SpeakerGroup groupOf (SpeakerRole role) noexcept
{
    using R = SpeakerRole;
    switch (role)
    {
        case R::Left:
        case R::Right:
        case R::LeftCentre:
        case R::RightCentre: return SpeakerGroup::Front;
        case R::Centre:
        case R::Mono: return SpeakerGroup::Centre;
        case R::Lfe:
        case R::Lfe2: return SpeakerGroup::Lfe;
        case R::LeftSurround:
        case R::RightSurround:
        case R::LeftSurroundSide:
        case R::RightSurroundSide: return SpeakerGroup::SideSurround;
        case R::LeftSurroundRear:
        case R::RightSurroundRear:
        case R::CentreSurround: return SpeakerGroup::RearSurround;
        case R::LeftWide:
        case R::RightWide: return SpeakerGroup::Wides;
        case R::TopFrontLeft:
        case R::TopFrontRight:
        case R::TopFrontCentre: return SpeakerGroup::TopFront;
        case R::TopMiddleLeft:
        case R::TopMiddleRight:
        case R::TopMiddleCentre: return SpeakerGroup::TopMiddle;
        case R::TopRearLeft:
        case R::TopRearRight:
        case R::TopRearCentre: return SpeakerGroup::TopRear;
        case R::Unknown:
        case R::BottomFrontLeft:
        case R::BottomFrontRight:
        case R::BottomFrontCentre:
        case R::ProximityLeft:
        case R::ProximityRight:
        case R::BottomSideLeft:
        case R::BottomSideRight:
        case R::BottomRearLeft:
        case R::BottomRearRight:
        case R::BottomRearCentre: break;
    }
    return SpeakerGroup::Other;
}

ChannelTopology ChannelTopology::fromRoles (const SpeakerRole* roles, int numChannels) noexcept
{
    ChannelTopology t;
    t.numChannels = std::clamp (numChannels, 0, kMaxChannels);
    std::copy_n (roles, t.numChannels, t.roles.begin());
    t.pairIndex.fill (-1);
    t.groups.fill (SpeakerGroup::Other);
    for (int ch = 0; ch < t.numChannels; ++ch)
        t.groups[static_cast<std::size_t> (ch)] = groupOf (t.roles[static_cast<std::size_t> (ch)]);

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
const char* speakerLabel (SpeakerRole role) noexcept
{
    using R = SpeakerRole;
    switch (role)
    {
        case R::Unknown: return "";
        case R::Mono: return "M";
        case R::Left: return "L";
        case R::Right: return "R";
        case R::Centre: return "C";
        case R::Lfe: return "LFE";
        case R::Lfe2: return "LFE2";
        case R::LeftSurround: return "Ls";
        case R::RightSurround: return "Rs";
        case R::LeftSurroundSide: return "Lss";
        case R::RightSurroundSide: return "Rss";
        case R::LeftSurroundRear: return "Lrs";
        case R::RightSurroundRear: return "Rrs";
        case R::CentreSurround: return "Cs";
        case R::LeftCentre: return "Lc";
        case R::RightCentre: return "Rc";
        case R::LeftWide: return "Lw";
        case R::RightWide: return "Rw";
        case R::TopFrontLeft: return "Ltf";
        case R::TopFrontRight: return "Rtf";
        case R::TopFrontCentre: return "Ctf";
        case R::TopMiddleLeft: return "Ltm";
        case R::TopMiddleRight: return "Rtm";
        case R::TopMiddleCentre: return "Tc";
        case R::TopRearLeft: return "Ltr";
        case R::TopRearRight: return "Rtr";
        case R::TopRearCentre: return "Ctr";
        case R::BottomFrontLeft: return "Lbf";
        case R::BottomFrontRight: return "Rbf";
        case R::BottomFrontCentre: return "Cbf";
        case R::ProximityLeft: return "Lp";
        case R::ProximityRight: return "Rp";
        case R::BottomSideLeft: return "Lbs";
        case R::BottomSideRight: return "Rbs";
        case R::BottomRearLeft: return "Lbr";
        case R::BottomRearRight: return "Rbr";
        case R::BottomRearCentre: return "Cbr";
    }
    return "";
}

bool SpeakerSelection::selects (const ChannelTopology& topology, int ch) const noexcept
{
    if (ch < 0 || ch >= topology.numChannels)
        return false;
    const auto role = topology.roles[static_cast<std::size_t> (ch)];
    if (role == SpeakerRole::Unknown)
        return ch < 32 && ((discrete >> ch) & 1u) != 0;
    return (roles & speakerBit (role)) != 0;
}

std::uint64_t SpeakerSelection::channelMask (const ChannelTopology& topology) const noexcept
{
    std::uint64_t mask = 0;
    for (int ch = 0; ch < topology.numChannels; ++ch)
        if (selects (topology, ch))
            mask |= std::uint64_t { 1 } << ch;
    return mask;
}

int SpeakerSelection::count (const ChannelTopology& topology) const noexcept
{
    int n = 0;
    for (int ch = 0; ch < topology.numChannels; ++ch)
        n += selects (topology, ch) ? 1 : 0;
    return n;
}
} // namespace ssc::engine
