#include "ssc/engine/BandProcessor.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace ssc::engine
{
namespace
{
    bool selected (std::uint64_t mask, bool includeLfe, const ChannelTopology& t, int ch) noexcept
    {
        if (ch >= 64 || ((mask >> ch) & 1u) == 0)
            return false;
        return includeLfe || ! t.isLfe (ch);
    }

    /** Moves value towards target by factor (1 − alpha); snaps when close. */
    double approach (double value, double target, double alpha, double tolerance) noexcept
    {
        const double next = target + (value - target) * alpha;
        return std::abs (next - target) <= tolerance ? target : next;
    }

    /** Dynamics are evaluated about every 1/12000 s, which keeps attack and release accurate. */
    int dynamicStepFor (double sampleRate) noexcept
    {
        return std::max (1, static_cast<int> (std::lround (sampleRate / 12000.0)));
    }

    /** Context of the Relative threshold of bell-like bands: a band pass this many times wider. */
    constexpr double kContextWidth = 4.0;

    double cascade (const filters::BandDesign& design, std::array<filters::SectionState, filters::kMaxSections>& states, double x) noexcept
    {
        for (int s = 0; s < design.numSections; ++s)
        {
            const auto si = static_cast<std::size_t> (s);
            x = filters::processSection (design.svf[si], design.digital[si].firstOrder, states[si], x);
        }
        return x;
    }

    double laneSample (const Lane& lane, const double* const* source, std::size_t index) noexcept
    {
        switch (lane.kind)
        {
            case Lane::Kind::Channel: return source[lane.a][index];
            case Lane::Kind::Mid: return 0.5 * (source[lane.a][index] + source[lane.b][index]);
            case Lane::Kind::Side: return 0.5 * (source[lane.a][index] - source[lane.b][index]);
        }
        return 0.0;
    }
} // namespace

filters::BandShape auditionShapeFor (const filters::BandShape& band) noexcept
{
    filters::BandShape a;
    a.frequencyHz = band.frequencyHz;
    a.slope = Slope::Db12;
    switch (band.shape)
    {
        case Shape::LowShelf:
        case Shape::LowCut:
            a.shape = Shape::HighCut; // the region below the band frequency
            a.q = 0.70710678;
            break;
        case Shape::HighShelf:
        case Shape::HighCut:
            a.shape = Shape::LowCut; // the region above the band frequency
            a.q = 0.70710678;
            break;
        case Shape::TiltShelf:
        case Shape::FlatTilt:
            a.shape = Shape::BandPass; // a broad region around the pivot
            a.q = 0.5;
            a.slope = Slope::Db6;
            break;
        case Shape::Bell:
        case Shape::Notch:
        case Shape::BandPass:
        case Shape::AllPass:
            a.shape = Shape::BandPass;
            a.q = std::clamp (band.q, 0.5, 20.0);
            a.slope = Slope::Db6;
            break;
    }
    return a;
}

LaneLayout makeLanes (StereoComponent component, std::uint64_t mask, bool includeLfe, const ChannelTopology& t) noexcept
{
    LaneLayout layout;
    auto push = [&layout] (Lane::Kind kind, int a, int b = -1) {
        if (layout.numLanes < kMaxChannels)
            layout.lanes[static_cast<std::size_t> (layout.numLanes++)] = { kind, a, b };
    };

    // Without pairs (mono, discrete) Left/Right/Mid act on every channel and Side does nothing.
    if (t.numPairs == 0)
    {
        if (component == StereoComponent::Side)
            return layout;
        for (int ch = 0; ch < t.numChannels; ++ch)
            if (selected (mask, includeLfe, t, ch))
                push (Lane::Kind::Channel, ch);
        return layout;
    }

    switch (component)
    {
        case StereoComponent::All:
            for (int ch = 0; ch < t.numChannels; ++ch)
                if (selected (mask, includeLfe, t, ch))
                    push (Lane::Kind::Channel, ch);
            break;

        case StereoComponent::Left:
        case StereoComponent::Right:
            for (int p = 0; p < t.numPairs; ++p)
            {
                const auto& pair = t.pairs[static_cast<std::size_t> (p)];
                const int ch = component == StereoComponent::Left ? pair.left : pair.right;
                if (selected (mask, includeLfe, t, ch))
                    push (Lane::Kind::Channel, ch);
            }
            break;

        case StereoComponent::Mid:
        case StereoComponent::Side:
            for (int p = 0; p < t.numPairs; ++p)
            {
                const auto& pair = t.pairs[static_cast<std::size_t> (p)];
                if (selected (mask, includeLfe, t, pair.left) && selected (mask, includeLfe, t, pair.right))
                    push (component == StereoComponent::Mid ? Lane::Kind::Mid : Lane::Kind::Side, pair.left, pair.right);
            }
            if (component == StereoComponent::Mid)
                for (int ch = 0; ch < t.numChannels; ++ch)
                    if (t.pairIndex[static_cast<std::size_t> (ch)] < 0 && selected (mask, includeLfe, t, ch))
                        push (Lane::Kind::Channel, ch); // the Mid of a single channel is the channel itself
            break;
    }

    return layout;
}

int linkKey (const Lane& lane, int laneIndex, DetectorLink link, const ChannelTopology& t) noexcept
{
    const bool hasChannel = lane.a >= 0 && lane.a < kMaxChannels;
    switch (link)
    {
        case DetectorLink::Independent: return laneIndex;
        case DetectorLink::Pair:
            if (lane.kind == Lane::Kind::Channel && hasChannel && t.pairIndex[static_cast<std::size_t> (lane.a)] >= 0)
                return 1000 + t.pairIndex[static_cast<std::size_t> (lane.a)];
            return laneIndex;
        case DetectorLink::Group: return hasChannel ? 2000 + static_cast<int> (t.groups[static_cast<std::size_t> (lane.a)]) : laneIndex;
        case DetectorLink::All: return 3000;
    }
    return laneIndex;
}

void BandProcessor::Instance::clearState() noexcept
{
    for (auto& lane : state)
        lane.fill ({});
}

void BandProcessor::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maxBlockSize);
    fadeLength = std::max (1, static_cast<int> (std::lround (0.01 * sampleRate)));
    dynamicStep = dynamicStepFor (sampleRate);
    gainTable.prepare (sampleRate);

    fadeA.assign (static_cast<std::size_t> (kMaxChannels), std::vector<double> (static_cast<std::size_t> (kMaxControlBlock), 0.0));
    fadeB.assign (static_cast<std::size_t> (kMaxChannels), std::vector<double> (static_cast<std::size_t> (kMaxControlBlock), 0.0));
    fadeAPtr.resize (fadeA.size());
    fadeBPtr.resize (fadeB.size());
    for (std::size_t i = 0; i < fadeA.size(); ++i)
    {
        fadeAPtr[i] = fadeA[i].data();
        fadeBPtr[i] = fadeB[i].data();
    }

    reset();
}

void BandProcessor::reset() noexcept
{
    for (auto& inst : instances)
    {
        inst.design = {};
        inst.lanes = {};
        inst.topologyKey = 0;
        inst.dynamic = false;
        inst.clearState();
    }
    active = 0;
    fadeRemaining = 0;
    hasSmoothed = false;
    detectValid = false;
    resetDynamics();
}

void BandProcessor::resetDynamics() noexcept
{
    for (auto& lane : dynLanes)
        lane = DynamicLane {};
    stepCounter = 1;
    lastAttackMs = lastReleaseMs = lastDetectorHz = -1.0;
    dynamicGainDb.store (0.0f, std::memory_order_relaxed);
}

void BandProcessor::advanceSmoothing (const BandSettings& target, int blockSize) noexcept
{
    const auto& t = target.shape;
    if (! hasSmoothed)
    {
        smoothed = t;
        blockStartGainDb = t.gainDb;
        hasSmoothed = true;
        return;
    }

    blockStartGainDb = smoothed.gainDb;
    const double alpha = std::exp (-blockSize / (smoothingTimeSeconds * sampleRate));
    smoothed.shape = t.shape;
    smoothed.slope = t.slope;
    smoothed.frequencyHz = std::exp (approach (std::log (smoothed.frequencyHz), std::log (t.frequencyHz), alpha, 1.0e-6));
    smoothed.gainDb = approach (smoothed.gainDb, t.gainDb, alpha, 1.0e-4);
    smoothed.q = std::exp (approach (std::log (smoothed.q), std::log (t.q), alpha, 1.0e-6));

    // Snap exactly so a settled band is designed from the exact parameter values.
    if (std::abs (std::log (smoothed.frequencyHz / t.frequencyHz)) <= 1.0e-6)
        smoothed.frequencyHz = t.frequencyHz;
    if (std::abs (std::log (smoothed.q / t.q)) <= 1.0e-6)
        smoothed.q = t.q;
}

void BandProcessor::updateDetection (const BandSettings& settings) noexcept
{
    const auto region = auditionShapeFor (smoothed);
    if (! detectValid || ! (region == detectShape))
    {
        detectShape = region;
        detectDesign = filters::designBand (region, sampleRate);

        // Relative threshold context: a wider band pass for bell-like regions, the whole lane otherwise.
        contextDesign = {};
        if (region.shape == Shape::BandPass)
        {
            auto context = region;
            context.q = std::max (0.1, region.q / kContextWidth);
            contextDesign = filters::designBand (context, sampleRate);
        }
        detectValid = true;
    }

    const double detectorHz = smoothed.frequencyHz;
    if (std::abs (detectorHz - lastDetectorHz) > 0.01 * detectorHz)
    {
        lastDetectorHz = detectorHz;
        for (auto& lane : dynLanes)
        {
            lane.level.setTime (dynamics::detectorTimeFor (detectorHz), sampleRate);
            lane.contextLevel.setTime (dynamics::detectorTimeFor (detectorHz), sampleRate);
        }
    }

    const auto& dyn = settings.dynamics;
    constexpr std::equal_to<double> same;
    if (! same (dyn.attackMs, lastAttackMs) || ! same (dyn.releaseMs, lastReleaseMs))
    {
        lastAttackMs = dyn.attackMs;
        lastReleaseMs = dyn.releaseMs;
        const double updateRate = sampleRate / dynamicStep;
        for (auto& lane : dynLanes)
            lane.smoother.setTimes (0.001 * dyn.attackMs, 0.001 * dyn.releaseMs, updateRate);
    }
}

void BandProcessor::runDetection (const BandSettings& settings, const Instance& inst, const ChannelTopology& topology,
                                  const DetectionInput& detection, const double* const* blockInput, int offset, int numSamples) noexcept
{
    const auto& dyn = settings.dynamics;
    const auto computer = dynamics::GainComputer::make (dyn.thresholdDb, dyn.ratio, dyn.kneeDb, dyn.rangeDb, dyn.direction);
    const bool relative = dyn.thresholdMode == ThresholdMode::Relative;
    const double relativeOffset = contextDesign.numSections > 0 ? 10.0 * std::log10 (kContextWidth) : 0.0;
    const int numLanes = inst.lanes.numLanes;

    // Lanes with the same link key share one level: map each lane to the first lane of its set.
    std::array<int, kMaxChannels> representative {};
    std::array<int, kMaxChannels> keys {};
    for (int l = 0; l < numLanes; ++l)
    {
        const auto li = static_cast<std::size_t> (l);
        keys[li] = linkKey (inst.lanes.lanes[li], l, dyn.link, topology);
        representative[li] = l;
        for (int m = 0; m < l; ++m)
            if (keys[static_cast<std::size_t> (m)] == keys[li])
            {
                representative[li] = m;
                break;
            }
    }

    // Detection reads the undelayed input when available (lookahead), otherwise the band input.
    const bool fromRings = detection.rings != nullptr;
    const int lookahead = std::clamp (static_cast<int> (std::lround (0.001 * dyn.lookaheadMs * sampleRate)), 0, detection.mainDelay);
    const int detectionDelay = detection.mainDelay - lookahead;

    std::array<double, kMaxChannels> levels {};
    for (int i = 0; i < numSamples; ++i)
    {
        for (int l = 0; l < numLanes; ++l)
        {
            const auto li = static_cast<std::size_t> (l);
            const auto& lane = inst.lanes.lanes[li];
            const double x = fromRings
                ? laneSample (lane, detection.rings,
                              static_cast<std::size_t> ((detection.blockStart + offset + i - detectionDelay) & detection.mask))
                : laneSample (lane, blockInput, static_cast<std::size_t> (i));
            auto& d = dynLanes[li];
            d.level.process (cascade (detectDesign, d.detect, x));
            if (relative)
                d.contextLevel.process (contextDesign.numSections > 0 ? cascade (contextDesign, d.context, x) : x);
        }

        if (--stepCounter <= 0)
        {
            stepCounter = dynamicStep;
            for (int l = 0; l < numLanes; ++l)
            {
                const auto& d = dynLanes[static_cast<std::size_t> (l)];
                double level = d.level.levelDb();
                if (relative)
                    level = level - d.contextLevel.levelDb() + relativeOffset;
                levels[static_cast<std::size_t> (l)] = level;
            }
            // A linked set uses the loudest level of the set (research §4).
            for (int l = 0; l < numLanes; ++l)
            {
                const auto rep = static_cast<std::size_t> (representative[static_cast<std::size_t> (l)]);
                if (rep != static_cast<std::size_t> (l))
                    levels[rep] = std::max (levels[rep], levels[static_cast<std::size_t> (l)]);
            }
            for (int l = 0; l < numLanes; ++l)
            {
                auto& d = dynLanes[static_cast<std::size_t> (l)];
                const double linked = levels[static_cast<std::size_t> (representative[static_cast<std::size_t> (l)])];
                const double target = d.smoother.process (computer (linked));
                d.gainStep = (target - d.gain) / dynamicStep;
            }
        }

        for (int l = 0; l < numLanes; ++l)
        {
            auto& d = dynLanes[static_cast<std::size_t> (l)];
            d.gain += d.gainStep;
            laneGain[static_cast<std::size_t> (l)][static_cast<std::size_t> (i)] = d.gain;
        }
    }

    float extreme = 0.0f;
    for (int l = 0; l < numLanes; ++l)
    {
        const auto g = static_cast<float> (dynamicMix * dynLanes[static_cast<std::size_t> (l)].gain);
        if (std::abs (g) > std::abs (extreme))
            extreme = g;
    }
    dynamicGainDb.store (extreme, std::memory_order_relaxed);
}

void BandProcessor::runInstance (Instance& inst, double* const* buffers, int n, ParamSource source) noexcept
{
    if (inst.isIdentity())
        return;

    const int sections = inst.design.numSections;
    const auto& digital = inst.design.digital;

    if (source == ParamSource::Interpolate)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i + 1) / n;
            for (int s = 0; s < sections; ++s)
                rampParams[static_cast<std::size_t> (i)][static_cast<std::size_t> (s)] =
                    filters::lerp (inst.params[static_cast<std::size_t> (s)], targetParams[static_cast<std::size_t> (s)], t);
        }
    }

    GainTable::Params dynamicParams = inst.params;
    for (int l = 0; l < inst.lanes.numLanes; ++l)
    {
        const auto li = static_cast<std::size_t> (l);
        auto& states = inst.state[li];
        const auto& gains = laneGain[li];

        auto filter = [&] (int i, double x) noexcept {
            const GainTable::Params* p = &inst.params;
            if (source == ParamSource::Interpolate)
            {
                p = &rampParams[static_cast<std::size_t> (i)];
            }
            else if (source == ParamSource::Dynamic)
            {
                const double staticGain = blockStartGainDb + (smoothed.gainDb - blockStartGainDb) * (i + 1) / n;
                gainTable.lookup (staticGain + dynamicMix * gains[static_cast<std::size_t> (i)], sections, dynamicParams);
                p = &dynamicParams;
            }
            for (int s = 0; s < sections; ++s)
            {
                const auto si = static_cast<std::size_t> (s);
                x = filters::processSection ((*p)[si], digital[si].firstOrder, states[si], x);
            }
            return x;
        };

        const auto& lane = inst.lanes.lanes[li];
        switch (lane.kind)
        {
            case Lane::Kind::Channel:
            {
                double* x = buffers[lane.a];
                for (int i = 0; i < n; ++i)
                    x[i] = filter (i, x[i]);
                break;
            }
            case Lane::Kind::Mid:
            case Lane::Kind::Side:
            {
                double* left = buffers[lane.a];
                double* right = buffers[lane.b];
                const bool mid = lane.kind == Lane::Kind::Mid;
                for (int i = 0; i < n; ++i)
                {
                    double m = 0.5 * (left[i] + right[i]);
                    double s = 0.5 * (left[i] - right[i]);
                    if (mid)
                        m = filter (i, m);
                    else
                        s = filter (i, s);
                    left[i] = m + s;
                    right[i] = m - s;
                }
                break;
            }
        }
    }

    if (source == ParamSource::Interpolate)
        for (int s = 0; s < sections; ++s)
            inst.params[static_cast<std::size_t> (s)] = targetParams[static_cast<std::size_t> (s)];
    else if (source == ParamSource::Dynamic)
        inst.params = dynamicParams; // a frozen state if the instance is faded out later
}

void BandProcessor::process (const BandSettings& settings, const ChannelTopology& topology, double* const* channels, int numChannels,
                             int numSamples, int controlBlock, const DetectionInput& detection) noexcept
{
    const int block = std::clamp (controlBlock, 1, kMaxControlBlock);
    const bool dynamicMode = settings.mode == BandMode::Dynamic && shapeHasGain (settings.shape.shape);
    dynamicMix = std::clamp (settings.dynamics.mix, 0.0, 1.0);

    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int n = std::min (block, numSamples - offset);
        double* buffers[kMaxChannels];
        for (int ch = 0; ch < numChannels; ++ch)
            buffers[ch] = channels[ch] + offset;

        advanceSmoothing (settings, n);

        // What the band should be now.
        const bool wantActive = settings.active && (dynamicMode || ! filters::isIdentity (smoothed));
        const LaneLayout lanes =
            wantActive ? makeLanes (settings.component, settings.channelMask, settings.includeLfe, topology) : LaneLayout {};
        auto& current = instances[static_cast<std::size_t> (active)];

        auto source = current.dynamic ? ParamSource::Dynamic : ParamSource::Frozen;
        if (fadeRemaining == 0)
        {
            const bool shapeChanged = ! (smoothed == designedShape);
            const bool wantDynamic = dynamicMode && wantActive;
            const bool modeChanged = current.dynamic != wantDynamic;
            filters::BandDesign design = current.design;
            if (wantActive && (shapeChanged || modeChanged || current.design.numSections == 0))
                design = filters::designBand (dynamicMode ? withNonZeroGain (smoothed) : smoothed, sampleRate);
            else if (! wantActive)
                design = {};

            const int key = design.topology();
            if (key != current.topologyKey || ! (lanes == current.lanes))
            {
                // Topology change: prepare the other instance and crossfade to it.
                auto& next = instances[static_cast<std::size_t> (1 - active)];
                next.design = design;
                next.params = design.svf;
                next.lanes = lanes;
                next.topologyKey = key;
                next.dynamic = wantDynamic;
                next.clearState();
                fadeRemaining = fadeLength;
            }
            else if (wantActive && (shapeChanged || modeChanged))
            {
                current.design = design;
                current.dynamic = wantDynamic;
                if (wantDynamic)
                {
                    source = ParamSource::Dynamic;
                }
                else
                {
                    targetParams = design.svf;
                    source = ParamSource::Interpolate;
                }
            }
            designedShape = smoothed;
        }

        // Dynamic bands keep detecting during crossfades so their gain stays continuous.
        auto& target = fadeRemaining > 0 ? instances[static_cast<std::size_t> (1 - active)] : current;
        if (target.dynamic)
        {
            gainTable.setShape (smoothed);
            updateDetection (settings);
            runDetection (settings, target, topology, detection, buffers, offset, n);
        }
        else if (lastAttackMs >= 0.0)
        {
            resetDynamics(); // leaving Dynamic mode: the next activation starts from 0 dB
        }

        if (fadeRemaining > 0)
        {
            auto& next = instances[static_cast<std::size_t> (1 - active)];

            // Only channels used by either instance take part; all others stay bit-exact.
            bool touched[kMaxChannels] {};
            for (const auto* inst : { &current, &next })
                for (int l = 0; l < inst->lanes.numLanes; ++l)
                {
                    const auto& lane = inst->lanes.lanes[static_cast<std::size_t> (l)];
                    if (lane.a >= 0 && lane.a < numChannels)
                        touched[lane.a] = true;
                    if (lane.b >= 0 && lane.b < numChannels)
                        touched[lane.b] = true;
                }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (! touched[ch])
                    continue;
                std::copy (buffers[ch], buffers[ch] + n, fadeAPtr[static_cast<std::size_t> (ch)]);
                std::copy (buffers[ch], buffers[ch] + n, fadeBPtr[static_cast<std::size_t> (ch)]);
            }
            runInstance (current, fadeAPtr.data(), n, ParamSource::Frozen);
            runInstance (next, fadeBPtr.data(), n, next.dynamic ? ParamSource::Dynamic : ParamSource::Frozen);

            for (int i = 0; i < n; ++i)
            {
                const int done = fadeLength - fadeRemaining + i + 1;
                const double w = std::min (1.0, static_cast<double> (done) / fadeLength);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    if (! touched[ch])
                        continue;
                    const auto c = static_cast<std::size_t> (ch);
                    buffers[ch][i] = fadeA[c][static_cast<std::size_t> (i)] * (1.0 - w) + fadeB[c][static_cast<std::size_t> (i)] * w;
                }
            }

            fadeRemaining = std::max (0, fadeRemaining - n);
            if (fadeRemaining == 0)
            {
                active = 1 - active;
                instances[static_cast<std::size_t> (1 - active)].clearState();
            }
        }
        else
        {
            runInstance (current, buffers, n, source);
        }
    }
}
} // namespace ssc::engine
