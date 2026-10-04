#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "ssc/engine/BandProcessor.h"
#include "ssc/engine/ChannelTopology.h"

using namespace ssc;
using namespace ssc::engine;

namespace
{
using R = SpeakerRole;

ChannelTopology atmos714()
{
    const SpeakerRole roles[] = { R::Left,
                                  R::Right,
                                  R::Centre,
                                  R::Lfe,
                                  R::LeftSurroundSide,
                                  R::RightSurroundSide,
                                  R::LeftSurroundRear,
                                  R::RightSurroundRear,
                                  R::TopFrontLeft,
                                  R::TopFrontRight,
                                  R::TopRearLeft,
                                  R::TopRearRight };
    return ChannelTopology::fromRoles (roles, 12);
}

std::uint64_t channels (std::initializer_list<int> list)
{
    std::uint64_t mask = 0;
    for (const int ch : list)
        mask |= std::uint64_t { 1 } << ch;
    return mask;
}
} // namespace

TEST_CASE ("Speaker selection maps roles onto any layout (CH-4)", "[speakers][CH-4]")
{
    const auto t = atmos714();
    const SpeakerSelection all;
    CHECK (all.isDefault());
    CHECK_FALSE (all.includesLfe());
    CHECK (all.channelMask (t) == channels ({ 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11 })); // every speaker but LFE
    CHECK (all.count (t) == 11);

    SpeakerSelection sides;
    sides.roles = speakerBit (R::LeftSurroundSide) | speakerBit (R::RightSurroundSide);
    CHECK (sides.channelMask (t) == channels ({ 4, 5 }));
    CHECK (sides.channelMask (ChannelTopology::stereo()) == 0); // no side surrounds in stereo

    SpeakerSelection withLfe;
    withLfe.roles |= SpeakerSelection::kLfeRoles;
    CHECK (withLfe.includesLfe());
    CHECK (withLfe.channelMask (t) == channels ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }));

    // Discrete layouts select by index.
    SpeakerSelection discrete;
    discrete.discrete = 0b1010;
    CHECK (discrete.channelMask (ChannelTopology::discrete (6)) == channels ({ 1, 3 }));

    CHECK (std::string (speakerLabel (R::LeftSurroundSide)) == "Lss");
    CHECK (std::string (speakerLabel (R::TopRearRight)) == "Rtr");
    CHECK (std::string (speakerLabel (R::Lfe)) == "LFE");
}

TEST_CASE ("A stereo All + Side band works on the Side of every selected pair in 7.1.4 (CH-4)", "[speakers][CH-4]")
{
    const auto t = atmos714();
    const SpeakerSelection fromStereo; // default selection saved in a stereo session
    const auto side = makeLanes (StereoComponent::Side, fromStereo.channelMask (t), true, t);
    REQUIRE (side.numLanes == 5); // L/R, Lss/Rss, Lrs/Rrs, Ltf/Rtf, Ltr/Rtr
    for (int l = 0; l < side.numLanes; ++l)
        CHECK (side.lanes[static_cast<std::size_t> (l)].kind == Lane::Kind::Side);

    // Mid on a single channel is the channel itself; Side on a single does nothing.
    SpeakerSelection centre;
    centre.roles = speakerBit (R::Centre);
    const auto mid = makeLanes (StereoComponent::Mid, centre.channelMask (t), true, t);
    REQUIRE (mid.numLanes == 1);
    CHECK (mid.lanes[0].kind == Lane::Kind::Channel);
    CHECK (mid.lanes[0].a == 2);
    CHECK (makeLanes (StereoComponent::Side, centre.channelMask (t), true, t).numLanes == 0);

    // A pair is processed in M/S only when both of its speakers are selected.
    SpeakerSelection oneSide;
    oneSide.roles = speakerBit (R::LeftSurroundSide);
    CHECK (makeLanes (StereoComponent::Side, oneSide.channelMask (t), true, t).numLanes == 0);
    CHECK (makeLanes (StereoComponent::All, oneSide.channelMask (t), true, t).numLanes == 1);
}
