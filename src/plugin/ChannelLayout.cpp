#include "plugin/ChannelLayout.h"

#include <utility>

namespace ssc::plugin
{
engine::SpeakerRole speakerRoleFor (juce::AudioChannelSet::ChannelType type) noexcept
{
    using T = juce::AudioChannelSet;
    using R = engine::SpeakerRole;

    static constexpr std::pair<T::ChannelType, R> table[] = {
        { T::left, R::Left },
        { T::right, R::Right },
        { T::centre, R::Centre },
        { T::LFE, R::Lfe },
        { T::LFE2, R::Lfe2 },
        { T::leftSurround, R::LeftSurround },
        { T::rightSurround, R::RightSurround },
        { T::leftCentre, R::LeftCentre },
        { T::rightCentre, R::RightCentre },
        { T::centreSurround, R::CentreSurround },
        { T::leftSurroundSide, R::LeftSurroundSide },
        { T::rightSurroundSide, R::RightSurroundSide },
        { T::leftSurroundRear, R::LeftSurroundRear },
        { T::rightSurroundRear, R::RightSurroundRear },
        { T::wideLeft, R::LeftWide },
        { T::wideRight, R::RightWide },
        { T::topMiddle, R::TopMiddleCentre },
        { T::topFrontLeft, R::TopFrontLeft },
        { T::topFrontCentre, R::TopFrontCentre },
        { T::topFrontRight, R::TopFrontRight },
        { T::topSideLeft, R::TopMiddleLeft },
        { T::topSideRight, R::TopMiddleRight },
        { T::topRearLeft, R::TopRearLeft },
        { T::topRearCentre, R::TopRearCentre },
        { T::topRearRight, R::TopRearRight },
        { T::bottomFrontLeft, R::BottomFrontLeft },
        { T::bottomFrontCentre, R::BottomFrontCentre },
        { T::bottomFrontRight, R::BottomFrontRight },
        { T::proximityLeft, R::ProximityLeft },
        { T::proximityRight, R::ProximityRight },
        { T::bottomSideLeft, R::BottomSideLeft },
        { T::bottomSideRight, R::BottomSideRight },
        { T::bottomRearLeft, R::BottomRearLeft },
        { T::bottomRearCentre, R::BottomRearCentre },
        { T::bottomRearRight, R::BottomRearRight },
    };

    for (const auto& [channelType, role] : table)
        if (channelType == type)
            return role;
    return R::Unknown; // discrete and ambisonic channels
}

engine::ChannelTopology topologyFor (const juce::AudioChannelSet& layout)
{
    const int numChannels = std::min (layout.size(), kMaxChannels);
    if (numChannels == 1)
        return engine::ChannelTopology::mono();

    std::array<engine::SpeakerRole, kMaxChannels> roles {};
    roles.fill (engine::SpeakerRole::Unknown);
    for (int ch = 0; ch < numChannels; ++ch)
        roles[static_cast<std::size_t> (ch)] = speakerRoleFor (layout.getTypeOfChannel (ch));

    return engine::ChannelTopology::fromRoles (roles.data(), numChannels);
}
} // namespace ssc::plugin
