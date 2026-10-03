#include "ssc/engine/BandProcessor.h"

#include <algorithm>
#include <cmath>

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
} // namespace

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
        inst.clearState();
    }
    active = 0;
    fadeRemaining = 0;
    hasSmoothed = false;
}

void BandProcessor::advanceSmoothing (const BandSettings& target, int blockSize) noexcept
{
    const auto& t = target.shape;
    if (! hasSmoothed)
    {
        smoothed = t;
        hasSmoothed = true;
        return;
    }

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

void BandProcessor::runInstance (Instance& inst, double* const* buffers, int n, bool interpolate) noexcept
{
    if (inst.isIdentity())
        return;

    const int sections = inst.design.numSections;
    const auto& digital = inst.design.digital;

    if (interpolate)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i + 1) / n;
            for (int s = 0; s < sections; ++s)
                rampParams[static_cast<std::size_t> (i)][static_cast<std::size_t> (s)] =
                    filters::lerp (inst.params[static_cast<std::size_t> (s)], targetParams[static_cast<std::size_t> (s)], t);
        }
    }

    auto cascade = [&] (int laneIndex, int i, double x) noexcept {
        auto& states = inst.state[static_cast<std::size_t> (laneIndex)];
        const auto& p = interpolate ? rampParams[static_cast<std::size_t> (i)] : inst.params;
        for (int s = 0; s < sections; ++s)
        {
            const auto si = static_cast<std::size_t> (s);
            x = filters::processSection (p[si], digital[si].firstOrder, states[si], x);
        }
        return x;
    };

    for (int l = 0; l < inst.lanes.numLanes; ++l)
    {
        const auto& lane = inst.lanes.lanes[static_cast<std::size_t> (l)];
        switch (lane.kind)
        {
            case Lane::Kind::Channel:
            {
                double* x = buffers[lane.a];
                for (int i = 0; i < n; ++i)
                    x[i] = cascade (l, i, x[i]);
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
                        m = cascade (l, i, m);
                    else
                        s = cascade (l, i, s);
                    left[i] = m + s;
                    right[i] = m - s;
                }
                break;
            }
        }
    }

    if (interpolate)
        for (int s = 0; s < sections; ++s)
            inst.params[static_cast<std::size_t> (s)] = targetParams[static_cast<std::size_t> (s)];
}

void BandProcessor::process (const BandSettings& settings, const ChannelTopology& topology, double* const* channels, int numChannels,
                             int numSamples, int controlBlock) noexcept
{
    const int block = std::clamp (controlBlock, 1, kMaxControlBlock);

    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int n = std::min (block, numSamples - offset);
        double* buffers[kMaxChannels];
        for (int ch = 0; ch < numChannels; ++ch)
            buffers[ch] = channels[ch] + offset;

        advanceSmoothing (settings, n);

        // What the band should be now.
        const bool wantActive = settings.active && ! filters::isIdentity (smoothed);
        const LaneLayout lanes =
            wantActive ? makeLanes (settings.component, settings.channelMask, settings.includeLfe, topology) : LaneLayout {};
        auto& current = instances[static_cast<std::size_t> (active)];

        bool interpolate = false;
        if (fadeRemaining == 0)
        {
            const bool shapeChanged = ! (smoothed == designedShape);
            filters::BandDesign design = current.design;
            if (wantActive && (shapeChanged || current.design.numSections == 0))
                design = filters::designBand (smoothed, sampleRate);
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
                next.clearState();
                fadeRemaining = fadeLength;
            }
            else if (wantActive && shapeChanged)
            {
                current.design = design;
                targetParams = design.svf;
                interpolate = true;
            }
            designedShape = smoothed;
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
            runInstance (current, fadeAPtr.data(), n, false);
            runInstance (next, fadeBPtr.data(), n, false);

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
            runInstance (current, buffers, n, interpolate);
        }
    }
}
} // namespace ssc::engine
