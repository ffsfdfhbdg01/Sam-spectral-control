#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "ssc/engine/ChannelTopology.h"
#include "ssc/filters/BandDesign.h"
#include "ssc/filters/SvfSection.h"

namespace ssc::engine
{
/** Everything the engine needs to run one band, read once per host block. */
struct BandSettings
{
    bool active = false; ///< band used and switched on
    filters::BandShape shape;
    StereoComponent component = StereoComponent::All;
    std::uint64_t channelMask = ~std::uint64_t { 0 }; ///< selected channels (speaker panel, M5)
    bool includeLfe = false;
    BandMode mode = BandMode::Static;
    Character character = Character::Clear;

    bool operator== (const BandSettings&) const = default;
};

/** A signal processed by a band: a channel, or the Mid or Side component of a channel pair. */
struct Lane
{
    enum class Kind : std::uint8_t
    {
        Channel,
        Mid,
        Side
    };

    Kind kind = Kind::Channel;
    int a = -1; ///< channel, or left channel of the pair
    int b = -1; ///< right channel of the pair

    bool operator== (const Lane&) const = default;
};

struct LaneLayout
{
    std::array<Lane, kMaxChannels> lanes {};
    int numLanes = 0;

    bool operator== (const LaneLayout&) const = default;
};

/** Lanes for a band target on a given channel topology (research §4, CH-3/CH-4). */
LaneLayout makeLanes (StereoComponent component, std::uint64_t channelMask, bool includeLfe, const ChannelTopology& topology) noexcept;

/**
    Runs one EQ band on the engine's working buffers.

    Continuous parameters (frequency, gain, Q) are smoothed and the matched design is recomputed
    per control block with per-sample interpolation of the SVF parameters. Changes of filter
    topology (shape, slope, on/off, target) crossfade between two instances. Real-time safe after
    prepare().
*/
class BandProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Processes numSamples in place; controlBlock is the coefficient update interval (Quality). */
    void process (const BandSettings& settings, const ChannelTopology& topology, double* const* channels, int numChannels, int numSamples,
                  int controlBlock) noexcept;

    /** Design currently heard (after smoothing). */
    const filters::BandDesign& currentDesign() const noexcept { return instances[static_cast<std::size_t> (active)].design; }

    static constexpr int kMaxControlBlock = 64;

private:
    struct Instance
    {
        filters::BandDesign design;
        std::array<filters::SvfParams, filters::kMaxSections> params {}; ///< parameters at the end of the last block
        LaneLayout lanes;
        int topologyKey = 0;
        std::array<std::array<filters::SectionState, filters::kMaxSections>, kMaxChannels> state {};

        void clearState() noexcept;
        bool isIdentity() const noexcept { return design.numSections == 0 || lanes.numLanes == 0; }
    };

    void advanceSmoothing (const BandSettings& target, int blockSize) noexcept;
    void runInstance (Instance& instance, double* const* buffers, int numSamples, bool interpolate) noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 0;
    std::array<Instance, 2> instances {};
    int active = 0;
    int fadeRemaining = 0;
    int fadeLength = 480;

    filters::BandShape smoothed;
    filters::BandShape designedShape;
    bool hasSmoothed = false;
    double smoothingTimeSeconds = 0.02;

    std::array<filters::SvfParams, filters::kMaxSections> targetParams {};
    std::array<std::array<filters::SvfParams, filters::kMaxSections>, kMaxControlBlock> rampParams {};

    std::vector<std::vector<double>> fadeA, fadeB;
    std::vector<double*> fadeAPtr, fadeBPtr;
};
} // namespace ssc::engine
