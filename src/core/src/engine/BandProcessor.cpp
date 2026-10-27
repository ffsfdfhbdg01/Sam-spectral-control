#include "ssc/engine/BandProcessor.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "ssc/util/Math.h"

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

    bool sameLanes (const LaneLayout& a, const LaneLayout& b) noexcept
    {
        if (a.numLanes != b.numLanes)
            return false;
        for (int l = 0; l < a.numLanes; ++l)
            if (! (a.lanes[static_cast<std::size_t> (l)] == b.lanes[static_cast<std::size_t> (l)]))
                return false;
        return true;
    }

    const LaneLayout kNoLanes {};
    const filters::BandDesign kNoDesign {};

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
    shaperState.fill ({});
}

void BandProcessor::prepare (double newSampleRate, int maxBlockSize)
{
    maxBlock = std::max (1, maxBlockSize);
    gainTable.prepare (newSampleRate);
    sampleRate = 0.0;
    setSampleRate (newSampleRate);

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

void BandProcessor::setSampleRate (double newSampleRate) noexcept
{
    if (std::equal_to<double>() (newSampleRate, sampleRate))
        return;
    sampleRate = newSampleRate;
    fadeLength = std::max (1, static_cast<int> (std::lround (0.01 * sampleRate)));
    dynamicStep = dynamicStepFor (sampleRate);
    gainTable.setSampleRate (sampleRate);
    for (int k = 0; k < filters::kNumShaperKinds; ++k)
        shapers[static_cast<std::size_t> (k)] = filters::makeShaper (static_cast<filters::ShaperKind> (k), sampleRate);
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

    // Settled: nothing to compute (the common case).
    if (smoothed == t)
    {
        blockStartGainDb = smoothed.gainDb;
        return;
    }

    blockStartGainDb = smoothed.gainDb;
    const double alpha = std::exp (-blockSize / (smoothingTimeSeconds * sampleRate));
    smoothed.shape = t.shape;
    smoothed.slope = t.slope;
    smoothed.character = t.character;
    smoothed.analog = t.analog;
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

    // Make-up averaging per dynamics step; −70 dBFS as a mean square (full-scale sine = 0.5).
    const double makeupCoeff = std::exp (-dynamicStep / (std::max (0.01, dyn.autoGainSeconds) * sampleRate));
    constexpr double kMakeupGate = 0.5e-7;

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
                const double target = d.smoother.process (computer (linked), computer.slope);

                // Automatic make-up (AG-2): the energy-weighted mean of the change, averaged with the
                // Auto Gain speed and frozen in silence (below −70 dBFS), is given back.
                if (dyn.autoGain)
                {
                    const double energy = d.level.meanSquare;
                    if (energy > kMakeupGate)
                    {
                        d.weightedGain = makeupCoeff * d.weightedGain + (1.0 - makeupCoeff) * energy * target;
                        d.weight = makeupCoeff * d.weight + (1.0 - makeupCoeff) * energy;
                    }
                    if (d.weight > 0.0)
                        d.makeup = std::clamp (-d.weightedGain / d.weight, -dyn.rangeDb, dyn.rangeDb);
                }
                else
                {
                    d.weightedGain = d.weight = d.makeup = 0.0;
                }
                d.gainStep = (target + d.makeup - d.gain) / dynamicStep;
            }
            if (recorder != nullptr)
                recordModulation (inst);
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
        const auto g = static_cast<float> (dynamicMix * dynLanes[static_cast<std::size_t> (l)].gain); // includes the make-up
        if (std::abs (g) > std::abs (extreme))
            extreme = g;
    }
    dynamicGainDb.store (extreme, std::memory_order_relaxed);
}

void BandProcessor::recordModulation (const Instance& inst) noexcept
{
    // Gain modulation (research §13c, MTR-2): the fast part m of the linear band gain G — a
    // second-order Butterworth high-pass at max(fc/8, 20 Hz) — modulates the band's signal b (the
    // detection filter output) and adds products of power E[b²·m²]. The intended, slower action of
    // the dynamics is not counted.
    auto& rec = *recorder;
    const double cutoff = std::max (smoothed.frequencyHz / 8.0, 20.0);
    if (std::abs (cutoff - rec.modulationCutoff) > 1.0e-3 * cutoff)
    {
        rec.modulationCutoff = cutoff;
        rec.modulationG = std::tan (kPi * std::min (cutoff * dynamicStep / sampleRate, 0.45));
    }
    const double g = rec.modulationG;
    constexpr double k = 1.4142135623730951; // 2R of a Butterworth SVF
    const double d = 1.0 / (1.0 + g * (k + g));
    for (int l = 0; l < inst.lanes.numLanes; ++l)
    {
        const auto li = static_cast<std::size_t> (l);
        const auto& lane = dynLanes[li];
        const double gain = dbToGain (smoothed.gainDb + dynamicMix * lane.gain);
        double& s1 = rec.gainS1[li];
        double& s2 = rec.gainS2[li];
        if (! rec.modulationPrimed)
        {
            s1 = 0.0; // settled at a constant gain: no high-pass output
            s2 = gain;
        }
        const double high = (gain - (k + g) * s1 - s2) * d;
        const double band = g * high + s1;
        s1 = g * high + band;
        const double low = g * band + s2;
        s2 = g * band + low;
        const double weight = inst.lanes.lanes[li].kind == Lane::Kind::Channel ? 1.0 : 2.0;
        rec.modulationEnergy += weight * lane.level.meanSquare * high * high * dynamicStep;
    }
    rec.modulationSamples += dynamicStep;
    rec.modulationPrimed = true;
}

void BandProcessor::runInstance (Instance& inst, double* const* buffers, int n, ParamSource source, bool record) noexcept
{
    if (inst.isIdentity())
        return;

    const int sections = inst.design.numSections;
    const auto& digital = inst.design.digital;

    // Applies a per-sample filter to every lane (channels, or the Mid/Side of pairs).
    auto forEachLane = [&] (auto&& filter) {
        for (int l = 0; l < inst.lanes.numLanes; ++l)
        {
            const auto li = static_cast<std::size_t> (l);
            auto& states = inst.state[li];
            const auto& lane = inst.lanes.lanes[li];
            switch (lane.kind)
            {
                case Lane::Kind::Channel:
                {
                    double* x = buffers[lane.a];
                    for (int i = 0; i < n; ++i)
                        x[i] = filter (l, i, states, x[i]);
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
                            m = filter (l, i, states, m);
                        else
                            s = filter (l, i, states, s);
                        left[i] = m + s;
                        right[i] = m - s;
                    }
                    break;
                }
            }
        }
    };

    using States = std::array<filters::SectionState, filters::kMaxSections>;

    // Character (CHR-2): the products of the contribution of sections [from, sections) are added to
    // the output. Bands without a nonlinearity run the plain cascade.
    const int from = inst.design.shapedFrom;
    const auto& shaper = shapers[static_cast<std::size_t> (inst.design.shaper)];
    const bool shaping = from >= 0 && shaper.enabled;

    // Distortion meters (research §13c): the input of the nonlinearity (after its LF emphasis,
    // st.previous) of one lane into the ring, and the energy of the products of every lane. The
    // state lives in locals during the loops: no stores to the recorder per sample.
    BandRecorder* const rec = record && shaping ? recorder : nullptr;
    float* const ring = rec != nullptr ? rec->ring.data() : nullptr;
    const int ringLane = rec != nullptr ? rec->lane : -1;
    const int ringStart = rec != nullptr ? rec->position : 0;
    int ringPosition = ringStart;
    int energyLane = -1;
    double energy = 0.0;
    if (rec != nullptr)
    {
        rec->kind = inst.design.shaper;
        rec->shapedThisBlock = true;
        for (int l = 0; l < inst.lanes.numLanes; ++l)
        {
            const auto li = static_cast<std::size_t> (l);
            rec->laneWeight[li] = inst.lanes.lanes[li].kind == Lane::Kind::Channel ? 1.0 : 2.0;
        }
    }
    auto recordSample = [&] (int l, double input, double products) noexcept {
        if (l != energyLane)
        {
            if (energyLane >= 0)
                rec->laneEnergy[static_cast<std::size_t> (energyLane)] += energy;
            energy = 0.0;
            energyLane = l;
        }
        energy += products * products;
        if (l == ringLane)
            ring[ringPosition++ & BandRecorder::kRingMask] = static_cast<float> (input);
    };
    auto finishRecording = [&]() noexcept {
        if (rec == nullptr)
            return;
        if (energyLane >= 0)
            rec->laneEnergy[static_cast<std::size_t> (energyLane)] += energy;
        rec->recorded += ringPosition - ringStart;
        rec->position = ringPosition & BandRecorder::kRingMask;
    };

    // Runs `section (s, states, x)` over the cascade of every lane, shaping when needed.
    auto runCascade = [&] (auto&& section) {
        if (! shaping)
        {
            forEachLane ([&] (int, int i, States& states, double x) noexcept {
                for (int s = 0; s < sections; ++s)
                    x = section (i, s, states, x);
                return x;
            });
            return;
        }
        forEachLane ([&] (int l, int i, States& states, double x) noexcept {
            double in = x;
            for (int s = 0; s < sections; ++s)
            {
                if (s == from)
                    in = x;
                x = section (i, s, states, x);
            }
            auto& st = inst.shaperState[static_cast<std::size_t> (l)];
            const double products = filters::shapeContribution (shaper, st, x - in);
            if (rec != nullptr)
                recordSample (l, st.previous, products);
            return x + products;
        });
    };

    if (source == ParamSource::Frozen)
    {
        // Steady state: coefficients prepared once for the whole block.
        std::array<filters::SvfPrepared, filters::kMaxSections> prepared {};
        for (int s = 0; s < sections; ++s)
            prepared[static_cast<std::size_t> (s)] =
                filters::prepareSection (inst.params[static_cast<std::size_t> (s)], digital[static_cast<std::size_t> (s)].firstOrder);
        runCascade ([&] (int, int s, States& states, double x) noexcept {
            const auto si = static_cast<std::size_t> (s);
            return filters::processPrepared (prepared[si], digital[si].firstOrder, states[si], x);
        });
        finishRecording();
        return;
    }

    if (source == ParamSource::Interpolate)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i + 1) / n;
            for (int s = 0; s < sections; ++s)
                rampParams[static_cast<std::size_t> (i)][static_cast<std::size_t> (s)] =
                    filters::lerp (inst.params[static_cast<std::size_t> (s)], targetParams[static_cast<std::size_t> (s)], t);
        }
        runCascade ([&] (int i, int s, States& states, double x) noexcept {
            const auto si = static_cast<std::size_t> (s);
            return filters::processSection (rampParams[static_cast<std::size_t> (i)][si], digital[si].firstOrder, states[si], x);
        });
        for (int s = 0; s < sections; ++s)
            inst.params[static_cast<std::size_t> (s)] = targetParams[static_cast<std::size_t> (s)];
        finishRecording();
        return;
    }

    // Dynamic: per lane and sample from the gain table.
    GainTable::Params dynamicParams = inst.params;
    auto dynamicSample = [&] (int l, int i, States& states, double x) noexcept {
        const double staticGain = blockStartGainDb + (smoothed.gainDb - blockStartGainDb) * (i + 1) / n;
        gainTable.lookup (staticGain + dynamicMix * laneGain[static_cast<std::size_t> (l)][static_cast<std::size_t> (i)], sections,
                          dynamicParams);
        const double in = x;
        for (int s = 0; s < sections; ++s)
        {
            const auto si = static_cast<std::size_t> (s);
            x = filters::processSection (dynamicParams[si], digital[si].firstOrder, states[si], x);
        }
        // Only shapes with gain are Dynamic: the whole band is shaped.
        if (shaping)
        {
            auto& st = inst.shaperState[static_cast<std::size_t> (l)];
            const double products = filters::shapeContribution (shaper, st, x - in);
            if (rec != nullptr)
                recordSample (l, st.previous, products);
            x += products;
        }
        return x;
    };
    forEachLane (dynamicSample);
    inst.params = dynamicParams; // a frozen state if the instance is faded out later
    finishRecording();
}

void BandProcessor::process (const BandSettings& settings, const ChannelTopology& topology, double* const* channels, int numChannels,
                             int numSamples, int controlBlock, const DetectionInput& detection) noexcept
{
    const int block = std::clamp (controlBlock, 1, kMaxControlBlock);
    const bool dynamicMode = settings.mode == BandMode::Dynamic && shapeHasGain (settings.shape.shape);
    dynamicMix = std::clamp (settings.dynamics.mix, 0.0, 1.0);

    // A band that is off and fully faded out costs nothing; it snaps to its settings when it returns.
    {
        const auto& current = instances[static_cast<std::size_t> (active)];
        if (! settings.active && fadeRemaining == 0 && current.isIdentity() && ! current.dynamic)
        {
            hasSmoothed = false;
            if (lastAttackMs >= 0.0)
                resetDynamics();
            return;
        }
    }

    // Lanes depend only on the settings and the topology: once per call.
    const LaneLayout activeLanes =
        settings.active ? makeLanes (settings.component, settings.channelMask, settings.includeLfe, topology) : LaneLayout {};

    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int n = std::min (block, numSamples - offset);
        double* buffers[kMaxChannels];
        for (int ch = 0; ch < numChannels; ++ch)
            buffers[ch] = channels[ch] + offset;

        advanceSmoothing (settings, n);

        // What the band should be now.
        const bool wantActive = settings.active && (dynamicMode || ! filters::isIdentity (smoothed));
        const LaneLayout& lanes = wantActive ? activeLanes : kNoLanes;
        auto& current = instances[static_cast<std::size_t> (active)];

        auto source = current.dynamic ? ParamSource::Dynamic : ParamSource::Frozen;
        if (fadeRemaining == 0)
        {
            const bool shapeChanged = ! (smoothed == designedShape);
            const bool wantDynamic = dynamicMode && wantActive;
            const bool modeChanged = current.dynamic != wantDynamic;
            const bool redesign = wantActive && (shapeChanged || modeChanged || current.design.numSections == 0);
            if (redesign)
                scratchDesign = filters::designBand (dynamicMode ? withNonZeroGain (smoothed) : smoothed, sampleRate);
            const filters::BandDesign& design = redesign ? scratchDesign : (wantActive ? current.design : kNoDesign);

            const int key = design.topology();
            if (key != current.topologyKey || ! sameLanes (lanes, current.lanes))
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
            else if (redesign)
            {
                current.design = scratchDesign;
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
            runInstance (current, buffers, n, source, recorder != nullptr);
        }

        // The recording must be gapless: a block without shaping (off, crossfade) starts it over.
        if (recorder != nullptr)
        {
            if (! recorder->shapedThisBlock)
                recorder->recorded = 0;
            recorder->shapedThisBlock = false;
        }
    }
}
} // namespace ssc::engine
