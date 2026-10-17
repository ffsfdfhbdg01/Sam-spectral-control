#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "ssc/dynamics/Dynamics.h"
#include "ssc/engine/ChannelTopology.h"
#include "ssc/engine/DistortionCapture.h"
#include "ssc/engine/GainTable.h"
#include "ssc/filters/BandDesign.h"
#include "ssc/filters/Character.h"
#include "ssc/filters/SvfSection.h"

namespace ssc::engine
{
/** Dynamic and Spectral settings of a band (research §6c, DYN-4). */
struct DynamicSettings
{
    ThresholdMode thresholdMode = ThresholdMode::Absolute;
    double thresholdDb = -20.0;
    double ratio = 2.0;
    Direction direction = Direction::Above;
    double rangeDb = 12.0;
    double attackMs = 5.0;
    double releaseMs = 100.0;
    double lookaheadMs = 0.0;
    double kneeDb = 6.0;
    double mix = 1.0;         ///< 0 … 1, scales the gain change (DYN-7)
    double selectivity = 0.5; ///< 0 … 1 (Spectral)
    DetectorLink link = DetectorLink::Group;
    bool sidechain = false;        ///< detection from the external sidechain input instead of the band's own signal
    bool autoGain = false;         ///< automatic make-up of the band (AG-2)
    double autoGainSeconds = 30.0; ///< averaging time of the make-up (global Auto Gain speed)
};

/** Everything the engine needs to run one band, read once per host block. */
struct BandSettings
{
    bool active = false; ///< band used and switched on
    bool used = false;   ///< band exists (on or off): decides whether the spectral engine runs (ADR-0011)
    filters::BandShape shape;
    StereoComponent component = StereoComponent::All;
    std::uint64_t channelMask = ~std::uint64_t { 0 }; ///< selected channels (speaker panel, M5)
    bool includeLfe = false;
    BandMode mode = BandMode::Static;
    DynamicSettings dynamics;

    /** True for a band that uses the spectral engine (Spectral mode on a shape with gain). */
    bool isSpectral() const noexcept { return used && mode == BandMode::Spectral && shapeHasGain (shape.shape); }
};

using BandSettingsArray = std::array<BandSettings, kNumBands>;

/**
    The undelayed engine input, or the external sidechain mapped onto the engine's channels, used by
    Dynamic bands as their detection signal.
    rings[ch] is a circular buffer (size mask + 1, a power of two); blockStart is the ring index of
    the newest input sample that corresponds to sample 0 of the processed block, whose main path
    is delayed by mainDelay samples (Max Lookahead, DYN-8).
*/
struct DetectionInput
{
    const double* const* rings = nullptr;
    int mask = 0;
    int blockStart = 0;
    int mainDelay = 0;
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
    Filter covering the region of the spectrum a band works on: used to audition ("solo") a band
    and as the detection filter of Dynamic bands. Bell-like shapes: band pass at fc and Q; shelves
    and cuts: the side of fc they act on.
*/
filters::BandShape auditionShapeFor (const filters::BandShape& band) noexcept;

/** Detector link key of a lane: lanes with equal keys share one detection level (CH-5). */
int linkKey (const Lane& lane, int laneIndex, DetectorLink link, const ChannelTopology& topology) noexcept;

/**
    Runs one EQ band on the engine's working buffers.

    Static: continuous parameters (frequency, gain, Q) are smoothed and the matched design is
    recomputed per control block with per-sample interpolation of the SVF parameters.
    Dynamic: the detection filter (the band's region) runs on the undelayed input, an RMS
    detector feeds the gain computer and attack/release smoothing, and the band gain is modulated
    per sample through a gain table of SVF parameters, so the response at every gain equals the
    static design (research §6c).
    Changes of filter topology (shape, slope, on/off, target) crossfade between two instances.
    Real-time safe after prepare().
*/
class BandProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Changes the processing rate (e.g. oversampling) without allocating; resets the band. */
    void setSampleRate (double sampleRate) noexcept;

    /** Processes numSamples in place; controlBlock is the coefficient update interval (Quality). */
    void process (const BandSettings& settings, const ChannelTopology& topology, double* const* channels, int numChannels, int numSamples,
                  int controlBlock, const DetectionInput& detection = {}) noexcept;

    /** Design currently heard (after smoothing). */
    const filters::BandDesign& currentDesign() const noexcept { return instances[static_cast<std::size_t> (active)].design; }

    /** Current dynamic gain change in dB (the lane with the largest change); any thread (DYN-11). */
    float getDynamicGainDb() const noexcept { return dynamicGainDb.load (std::memory_order_relaxed); }

    /** Applied dynamic gain change of one lane, make-up included (tests). */
    double getLaneGainDb (int lane) const noexcept { return dynLanes[static_cast<std::size_t> (lane)].gain; }

    /** Automatic make-up of one lane (AG-2). */
    double getLaneMakeupDb (int lane) const noexcept { return dynLanes[static_cast<std::size_t> (lane)].makeup; }

    /** Records the nonlinearity and the gain modulation for the distortion meters (research §13c);
        nullptr stops recording. Audio thread, or before processing starts. */
    void setRecorder (BandRecorder* r) noexcept { recorder = r; }

    static constexpr int kMaxControlBlock = 64;

private:
    struct Instance
    {
        filters::BandDesign design;
        std::array<filters::SvfParams, filters::kMaxSections> params {}; ///< parameters at the end of the last block
        LaneLayout lanes;
        int topologyKey = 0;
        bool dynamic = false;
        std::array<std::array<filters::SectionState, filters::kMaxSections>, kMaxChannels> state {};
        std::array<filters::ShaperState, kMaxChannels> shaperState {}; ///< Character nonlinearity per lane

        void clearState() noexcept;
        bool isIdentity() const noexcept { return design.numSections == 0 || lanes.numLanes == 0; }
    };

    struct DynamicLane
    {
        std::array<filters::SectionState, filters::kMaxSections> detect {};
        std::array<filters::SectionState, filters::kMaxSections> context {};
        dynamics::RmsDetector level, contextLevel;
        dynamics::GainSmoother smoother;
        double gain = 0.0;                       ///< applied gain change (dB), ramped per sample
        double gainStep = 0.0;                   ///< per-sample increment towards the latest smoothed value
        double weightedGain = 0.0, weight = 0.0; ///< energy-weighted mean of the change (AG-2)
        double makeup = 0.0;                     ///< automatic make-up (dB), applied with the change
    };

    enum class ParamSource
    {
        Frozen,      ///< params of the instance, unchanged
        Interpolate, ///< from params to targetParams over the block
        Dynamic      ///< per lane and sample from the gain table
    };

    void advanceSmoothing (const BandSettings& target, int blockSize) noexcept;
    void runInstance (Instance& instance, double* const* buffers, int numSamples, ParamSource source, bool record = false) noexcept;
    void recordModulation (const Instance& instance) noexcept;
    void updateDetection (const BandSettings& settings) noexcept;
    void runDetection (const BandSettings& settings, const Instance& instance, const ChannelTopology& topology,
                       const DetectionInput& detection, const double* const* blockInput, int offset, int numSamples) noexcept;
    void resetDynamics() noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 0;
    std::array<filters::Shaper, filters::kNumShaperKinds> shapers {}; ///< per ShaperKind
    std::array<Instance, 2> instances {};
    int active = 0;
    int fadeRemaining = 0;
    int fadeLength = 480;

    filters::BandShape smoothed;
    filters::BandShape designedShape;
    filters::BandDesign scratchDesign;
    bool hasSmoothed = false;
    double smoothingTimeSeconds = 0.02;
    double blockStartGainDb = 0.0; ///< smoothed static gain at the start of the current block

    std::array<filters::SvfParams, filters::kMaxSections> targetParams {};
    std::array<std::array<filters::SvfParams, filters::kMaxSections>, kMaxControlBlock> rampParams {};

    // Dynamic mode
    GainTable gainTable;
    filters::BandDesign detectDesign, contextDesign;
    filters::BandShape detectShape;
    bool detectValid = false;
    std::array<DynamicLane, kMaxChannels> dynLanes {};
    std::array<std::array<double, kMaxControlBlock>, kMaxChannels> laneGain {};
    int dynamicStep = 4;
    int stepCounter = 1;
    double dynamicMix = 1.0;
    double lastAttackMs = -1.0, lastReleaseMs = -1.0, lastDetectorHz = -1.0;
    std::atomic<float> dynamicGainDb { 0.0f };

    std::vector<std::vector<double>> fadeA, fadeB;
    std::vector<double*> fadeAPtr, fadeBPtr;

    BandRecorder* recorder = nullptr; ///< distortion meters (owned by the engine)
};
} // namespace ssc::engine
