#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "AllocationCounter.h"
#include "TestSignals.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/SpeakerState.h"
#include "ssc/spectral/SpectralEngine.h"

/*
    Stress tests (TST-4): every channel layout, 44.1–384 kHz, host blocks of 1–8192 samples and of
    varying size, fast automation of every parameter, layout and sample rate changes during a
    session. Each run checks that the output is finite and bounded, that no block was muted for a
    non-finite value, and that processBlock() made no heap allocation.
*/

using namespace ssc;
using plugin::BandParam;
using plugin::bandParamId;
using plugin::SscAudioProcessor;
using S = juce::AudioChannelSet;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

constexpr double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0 };

void setPlain (SscAudioProcessor& p, const juce::String& paramId, float plain)
{
    auto* param = p.getValueTreeState().getParameter (paramId);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

template <typename Enum>
float index (Enum value)
{
    return static_cast<float> (value);
}

bool setLayout (SscAudioProcessor& p, const S& set)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    return p.setBusesLayout (layout);
}

/** Every layout the plugin accepts (CH-1). */
std::vector<S> acceptedLayouts()
{
    return { S::mono(),
             S::stereo(),
             S::createLCR(),
             S::createLCRS(),
             S::quadraphonic(),
             S::create5point0(),
             S::create5point1(),
             S::create6point1(),
             S::create7point0(),
             S::create7point1(),
             S::create5point1point2(),
             S::create5point1point4(),
             S::create7point1point2(),
             S::create7point1point4(),
             S::create7point1point6(),
             S::create9point1point4(),
             S::create9point1point6(),
             S::discreteChannels (1),
             S::discreteChannels (24),
             S::discreteChannels (32) };
}

/** A demanding setting: every band mode, nonlinear Character, M/S and single-channel targets,
    a lookahead, Auto Gain on the bands and globally. */
void heavySetting (SscAudioProcessor& p, PhaseMode phaseMode, Oversampling oversampling, Quality quality)
{
    setPlain (p, plugin::id::phaseMode, index (phaseMode));
    setPlain (p, plugin::id::oversampling, index (oversampling));
    setPlain (p, plugin::id::quality, index (quality));
    setPlain (p, plugin::id::analogModel, index (AnalogModel::N));
    setPlain (p, plugin::id::autoGain, 1.0f);
    setPlain (p, plugin::id::maxLookahead, 5.0f);

    struct Spec
    {
        Shape shape;
        float hz, gainDb, q;
        Slope slope;
        BandMode mode;
        StereoComponent target;
        Character character;
    };
    const Spec specs[] = {
        { Shape::LowCut, 30.0f, 0.0f, 0.7f, Slope::Db24, BandMode::Static, StereoComponent::All, Character::Clear },
        { Shape::Bell, 250.0f, -3.0f, 1.0f, Slope::Db12, BandMode::Dynamic, StereoComponent::All, Character::Clear },
        { Shape::Bell, 3000.0f, 4.0f, 2.0f, Slope::Db12, BandMode::Static, StereoComponent::Mid, Character::Warm },
        { Shape::HighShelf, 9000.0f, 3.0f, 0.7f, Slope::Db12, BandMode::Static, StereoComponent::Side, Character::Sharp },
        { Shape::Bell, 5000.0f, 0.0f, 0.5f, Slope::Db12, BandMode::Spectral, StereoComponent::All, Character::Clear },
        { Shape::Notch, 60.0f, 0.0f, 20.0f, Slope::Db12, BandMode::Static, StereoComponent::All, Character::Clear },
        { Shape::HighCut, 18000.0f, 0.0f, 0.7f, Slope::Db48, BandMode::Static, StereoComponent::All, Character::Clear },
        { Shape::Bell, 1000.0f, 2.0f, 1.5f, Slope::Db12, BandMode::Dynamic, StereoComponent::Left, Character::Warm },
    };
    for (int b = 0; b < static_cast<int> (std::size (specs)); ++b)
    {
        const auto& s = specs[b];
        auto set = [&p, b] (BandParam param, float value) { setPlain (p, bandParamId (b, param), value); };
        set (BandParam::Used, 1.0f);
        set (BandParam::Shape, index (s.shape));
        set (BandParam::Frequency, s.hz);
        set (BandParam::Gain, s.gainDb);
        set (BandParam::Q, s.q);
        set (BandParam::Slope, index (s.slope));
        set (BandParam::Mode, index (s.mode));
        set (BandParam::Target, index (s.target));
        set (BandParam::Character, index (s.character));
        if (s.mode == BandMode::Dynamic)
        {
            set (BandParam::Threshold, -30.0f);
            set (BandParam::Ratio, 3.0f);
            set (BandParam::Range, 9.0f);
            set (BandParam::Attack, 1.0f);
            set (BandParam::Release, 80.0f);
            set (BandParam::Lookahead, 3.0f);
            set (BandParam::AutoGain, 1.0f);
        }
        if (s.mode == BandMode::Spectral)
        {
            set (BandParam::ThresholdMode, index (ThresholdMode::Relative));
            set (BandParam::Threshold, 6.0f);
            set (BandParam::Range, 6.0f);
            set (BandParam::Selectivity, 70.0f);
        }
    }
}

/** Host block sizes: fixed, or uniformly random in [lo, hi] (0 = an empty block). */
class BlockSizes
{
public:
    static BlockSizes fixed (int size) { return { size, size }; }
    static BlockSizes random (int lo, int hi) { return { lo, hi }; }

    int next() { return lo == hi ? lo : std::uniform_int_distribution<int> (lo, hi) (rng); }
    int largest() const noexcept { return hi; }

private:
    BlockSizes (int l, int h)
        : lo (l),
          hi (h)
    {
    }

    int lo, hi;
    std::mt19937 rng { 2024 };
};

/** Test input of a channel: pink noise at −20 dBFS RMS, shifted and scaled per channel. */
float inputSample (const std::vector<float>& noise, int ch, std::int64_t i)
{
    const auto size = static_cast<std::int64_t> (noise.size());
    return noise[static_cast<std::size_t> ((i + 997 * ch) % size)] * (1.0f - 0.02f * static_cast<float> (ch));
}

struct Rendered
{
    std::vector<std::vector<float>> out; ///< [channel][sample]
    bool finite = true;
    bool subnormal = false;
    float peak = 0.0f;
    std::size_t allocations = 0;
};

/** Plays the test input through the processor in host blocks and collects the output.
    `position` is the input position, carried across calls. */
template <typename Sample = float>
Rendered play (SscAudioProcessor& p, const std::vector<float>& noise, int numSamples, BlockSizes sizes, std::int64_t& position,
               float gain = 1.0f)
{
    const int numChannels = p.getTotalNumOutputChannels();
    Rendered r;
    r.out.assign (static_cast<std::size_t> (numChannels), std::vector<float> (static_cast<std::size_t> (numSamples)));
    juce::AudioBuffer<Sample> buffer (numChannels, std::max (1, sizes.largest()));
    juce::MidiBuffer midi;
    int offset = 0;
    while (offset < numSamples)
    {
        const int n = std::min (sizes.next(), numSamples - offset);
        buffer.setSize (numChannels, n, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, static_cast<Sample> (gain * inputSample (noise, ch, position + i)));
        {
            const test::ScopedAllocationCounter counter;
            p.processBlock (buffer, midi);
            r.allocations += counter.count();
        }
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const auto y = static_cast<float> (buffer.getSample (ch, i));
                r.out[static_cast<std::size_t> (ch)][static_cast<std::size_t> (offset + i)] = y;
                r.finite = r.finite && std::isfinite (y);
                r.subnormal = r.subnormal || std::fpclassify (y) == FP_SUBNORMAL;
                r.peak = std::max (r.peak, std::abs (y));
            }
        offset += n;
        position += n;
    }
    return r;
}

/** Checks of every stress run: finite and bounded, nothing muted, no allocation. */
void checkHealthy (const SscAudioProcessor& p, const Rendered& r, float maxPeak = 8.0f)
{
    CHECK (r.finite);
    CHECK_FALSE (r.subnormal);
    CHECK (r.peak < maxPeak);
    CHECK (r.allocations == 0);
    CHECK (p.getNonFiniteEvents() == 0);
}

bool isLfe (const S& set, int ch)
{
    const auto type = set.getTypeOfChannel (ch);
    return type == S::LFE || type == S::LFE2;
}

double seconds (std::chrono::steady_clock::time_point since)
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now() - since).count();
}
} // namespace

TEST_CASE ("TST-4: the allocation counter sees allocations on its own thread only", "[plugin][TST-4]")
{
    const test::ScopedAllocationCounter counter;
    std::vector<int> v (100, 1);
    CHECK (counter.count() == 1);
    std::thread other ([] { std::vector<int> w (10, 2); });
    other.join();
    CHECK (counter.count() <= 2); // the thread object itself may allocate here, its vector does not count
    CHECK (v[99] == 1);
}

TEST_CASE ("TST-4: every channel layout runs the heavy setting; LFE stays an exact delay", "[plugin][TST-4][CH-1][CH-6]")
{
    JuceEnvironment env;
    const auto noise = test::pinkNoise (1 << 15);
    const auto start = std::chrono::steady_clock::now();
    for (const auto& set : acceptedLayouts())
    {
        CAPTURE (set.getDescription().toStdString());
        SscAudioProcessor p;
        REQUIRE (setLayout (p, set));
        heavySetting (p, PhaseMode::NaturalPhase, Oversampling::X2, Quality::High);
        p.prepareToPlay (48000.0, 512);
        REQUIRE (p.getTotalNumOutputChannels() == set.size());

        std::int64_t position = 0;
        const auto r = play (p, noise, 1 << 14, BlockSizes::fixed (512), position);
        checkHealthy (p, r);

        // No band selects the LFE by default: it is the input delayed by the latency (CH-6).
        const int latency = p.getLatencySamples();
        REQUIRE (latency > 0);
        for (int ch = 0; ch < set.size(); ++ch)
            if (isLfe (set, ch))
                for (int i = latency; i < (1 << 14); ++i)
                    REQUIRE (r.out[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] == inputSample (noise, ch, i - latency));
    }
    std::printf ("[TST-4] %zu layouts: %.1f s\n", acceptedLayouts().size(), seconds (start));
}

TEST_CASE ("TST-4: 44.1-384 kHz - latency as reported, dry path aligned, heavy setting healthy (PLT-4)", "[plugin][TST-4][PLT-4][OS-3]")
{
    JuceEnvironment env;
    const auto start = std::chrono::steady_clock::now();
    for (const double fs : kRates)
    {
        CAPTURE (fs);

        // The processed path: an impulse through a 0 dB band (and a Spectral band that never acts)
        // is centred on the reported latency (exactly for linear phase).
        for (const auto phaseMode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase })
            for (const auto os : { Oversampling::Off, Oversampling::X2, Oversampling::X8 })
                for (const auto quality : { Quality::Normal, Quality::Max })
                {
                    const bool spectral = quality == Quality::Max;
                    CAPTURE (static_cast<int> (phaseMode), static_cast<int> (os), static_cast<int> (quality));
                    SscAudioProcessor p;
                    setPlain (p, plugin::id::phaseMode, index (phaseMode));
                    setPlain (p, plugin::id::oversampling, index (os));
                    setPlain (p, plugin::id::quality, index (quality));
                    setPlain (p, bandParamId (0, BandParam::Used), 1.0f);
                    if (spectral)
                    {
                        setPlain (p, bandParamId (1, BandParam::Used), 1.0f);
                        setPlain (p, bandParamId (1, BandParam::Mode), index (BandMode::Spectral));
                        setPlain (p, bandParamId (1, BandParam::Threshold), 30.0f);
                    }
                    p.setPlayConfigDetails (2, 2, fs, 512);
                    p.prepareToPlay (fs, 512);
                    const int latency = p.getLatencySamples();
                    CHECK (p.getOversamplingFactor() == std::min (oversamplingFactor (os), engine::maxOversamplingFor (fs)));
                    if (spectral)
                        CHECK (latency >= spectral::fftSizeFor (Quality::Max, fs)); // the STFT runs

                    const int length = latency + static_cast<int> (0.05 * fs) + 512;
                    juce::AudioBuffer<float> buffer (2, length);
                    buffer.clear();
                    buffer.setSample (0, 100, 1.0f);
                    juce::MidiBuffer midi;
                    juce::AudioBuffer<float> block (2, 512);
                    for (int offset = 0; offset < length; offset += 512)
                    {
                        const int n = std::min (512, length - offset);
                        block.setSize (2, n, false, false, true);
                        for (int ch = 0; ch < 2; ++ch)
                            block.copyFrom (ch, 0, buffer, ch, offset, n);
                        p.processBlock (block, midi);
                        for (int ch = 0; ch < 2; ++ch)
                            buffer.copyFrom (ch, offset, block, ch, 0, n);
                    }
                    double m0 = 0.0, m1 = 0.0;
                    for (int i = 0; i < length; ++i)
                    {
                        m0 += buffer.getSample (0, i);
                        m1 += static_cast<double> (i) * buffer.getSample (0, i);
                    }
                    const double delay = m1 / m0 - 100.0;
                    const int factor = p.getOversamplingFactor();
                    if (phaseMode == PhaseMode::NaturalPhase || factor == 1)
                        CHECK (std::abs (delay - latency) < 1.0e-3);
                    else
                        CHECK (std::abs (delay - latency) <= 0.5 / factor + 1.0e-3);
                }

        // The heavy setting in every phase mode, at the largest oversampling the rate allows.
        const auto noise = test::pinkNoise (1 << 16);
        for (const auto phaseMode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase, PhaseMode::AnalogFeel })
        {
            CAPTURE (static_cast<int> (phaseMode));
            SscAudioProcessor p;
            heavySetting (p, phaseMode, Oversampling::X8, Quality::Max);
            p.setPlayConfigDetails (2, 2, fs, 512);
            p.prepareToPlay (fs, 512);
            const int latency = p.getLatencySamples();

            // Mix 0 %: the output is the input delayed by the reported latency (after the 20 ms ramp).
            setPlain (p, plugin::id::mix, 0.0f);
            std::int64_t position = 0;
            const int dryLength = latency + static_cast<int> (0.05 * fs);
            const auto dry = play (p, noise, dryLength, BlockSizes::fixed (512), position);
            const int settled = std::max (latency, static_cast<int> (0.03 * fs));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = settled; i < dryLength; ++i)
                    REQUIRE (dry.out[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] == inputSample (noise, ch, i - latency));

            setPlain (p, plugin::id::mix, 100.0f);
            const auto wet = play (p, noise, static_cast<int> (0.25 * fs), BlockSizes::fixed (512), position);
            checkHealthy (p, wet);
            CHECK (p.getLatencySamples() == latency);
        }
    }
    std::printf ("[TST-4] %zu sample rates: %.1f s\n", std::size (kRates), seconds (start));
}

TEST_CASE ("TST-4: host blocks of 1-8192 samples and of varying size give the same output (NFR-3)", "[plugin][TST-4][NFR-3]")
{
    JuceEnvironment env;
    const auto noise = test::pinkNoise (1 << 16);
    constexpr int kLength = 1 << 15;
    const auto start = std::chrono::steady_clock::now();

    auto render = [&noise] (int prepared, BlockSizes sizes, bool doublePrecision = false) {
        SscAudioProcessor p;
        heavySetting (p, PhaseMode::NaturalPhase, Oversampling::X2, Quality::High);
        if (doublePrecision)
            p.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        p.setPlayConfigDetails (2, 2, 48000.0, prepared);
        p.prepareToPlay (48000.0, prepared);
        std::int64_t position = 0;
        auto r = doublePrecision ? play<double> (p, noise, kLength, sizes, position) : play<float> (p, noise, kLength, sizes, position);
        checkHealthy (p, r);
        return r.out;
    };

    const auto reference = render (512, BlockSizes::fixed (512));
    {
        // Not a pass-through: the bands change the signal.
        SscAudioProcessor p;
        heavySetting (p, PhaseMode::NaturalPhase, Oversampling::X2, Quality::High);
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        const int latency = p.getLatencySamples();
        double difference = 0.0;
        for (int i = latency; i < kLength; ++i)
            difference = std::max (
                difference,
                std::abs (static_cast<double> (reference[0][static_cast<std::size_t> (i)]) - inputSample (noise, 0, i - latency)));
        CHECK (difference > 0.01);
    }
    for (const int size : { 1, 2, 3, 7, 16, 32, 64, 100, 128, 256, 441, 1000, 1024, 2048, 4096, 8192 })
    {
        CAPTURE (size);
        CHECK (render (size, BlockSizes::fixed (size)) == reference);
    }
    // Varying sizes, empty blocks, and blocks larger than announced in prepareToPlay().
    CHECK (render (512, BlockSizes::random (0, 8192)) == reference);
    CHECK (render (8192, BlockSizes::random (1, 8192)) == reference);
    CHECK (render (64, BlockSizes::random (1, 300)) == reference);

    // Double precision: the same independence.
    const auto referenceDouble = render (512, BlockSizes::fixed (512), true);
    CHECK (render (1, BlockSizes::fixed (1), true) == referenceDouble);
    CHECK (render (1024, BlockSizes::random (0, 4096), true) == referenceDouble);
    std::printf ("[TST-4] block sizes 1-8192: %.1f s\n", seconds (start));
}

TEST_CASE ("TST-4: fast automation of every parameter stays finite, allocation-free and stable", "[plugin][TST-4]")
{
    JuceEnvironment env;
    const auto noise = test::pinkNoise (1 << 16);
    const auto start = std::chrono::steady_clock::now();

    for (const auto& set : { S::stereo(), S::create5point1() })
    {
        CAPTURE (set.getDescription().toStdString());
        SscAudioProcessor p;
        REQUIRE (setLayout (p, set));
        heavySetting (p, PhaseMode::NaturalPhase, Oversampling::X2, Quality::Normal);
        p.prepareToPlay (48000.0, 256);

        std::mt19937 rng (static_cast<unsigned> (77 + set.size()));
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        juce::Array<juce::RangedAudioParameter*> automatable, other;
        for (auto* param : p.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            REQUIRE (ranged != nullptr);
            // Eight bands, so that random boosts stay far from the float range.
            const auto id = ranged->getParameterID();
            if (id.startsWith ("b") && id.substring (1, 3).getIntValue() > 8)
                continue;
            (ranged->isAutomatable() ? automatable : other).add (ranged);
        }
        REQUIRE (automatable.size() > 150);

        BlockSizes sizes = BlockSizes::random (1, 256);
        std::int64_t position = 0;
        Rendered total;
        for (int block = 0; block < 1500; ++block)
        {
            // Every automatable parameter moves in every block, as a host's automation lanes.
            for (auto* param : automatable)
                param->setValueNotifyingHost (unit (rng));
            // Settings that are not automatable change now and then (the user, a preset, A/B).
            if (block % 25 == 0)
                for (int k = 0; k < 3; ++k)
                    other[static_cast<int> (rng() % static_cast<unsigned> (other.size()))]->setValueNotifyingHost (unit (rng));
            if (block % 40 == 0)
                p.setSoloBand (static_cast<int> (rng() % 14) - 2);

            const auto r = play (p, noise, sizes.next(), BlockSizes::fixed (256), position, 0.5f);
            total.finite = total.finite && r.finite;
            total.subnormal = total.subnormal || r.subnormal;
            total.peak = std::max (total.peak, r.peak);
            total.allocations += r.allocations;
        }
        std::printf ("[TST-4] automation (%s): peak %.1f dBFS\n", set.getDescription().toRawUTF8(),
                     20.0 * std::log10 (std::max (1.0e-9f, total.peak)));
        checkHealthy (p, total, 1.0e9f);

        // Stable: with the automation stopped and the input silent, the output dies away, in the
        // last random setting and then in the heavy one.
        p.setSoloBand (-1);
        auto silence = [&] (int numSeconds) {
            std::vector<double> energy;
            for (int second = 0; second < numSeconds; ++second)
            {
                const auto r = play (p, noise, 48000, BlockSizes::fixed (256), position, 0.0f);
                double e = 0.0;
                for (const auto& channel : r.out)
                    for (const float y : channel)
                        e += static_cast<double> (y) * y;
                energy.push_back (e);
                CHECK (r.finite);
                CHECK_FALSE (r.subnormal);
                CHECK (r.allocations == 0);
            }
            return energy;
        };
        const auto randomTail = silence (8);
        CAPTURE (randomTail.front(), randomTail.back());
        CHECK (randomTail.back() <= 1.0e-2 * randomTail.front() + 1.0e-12);
        heavySetting (p, PhaseMode::NaturalPhase, Oversampling::X2, Quality::Normal);
        setPlain (p, plugin::id::bypass, 0.0f);
        setPlain (p, plugin::id::outputGain, 0.0f);
        setPlain (p, plugin::id::mix, 100.0f);
        const auto heavyTail = silence (4);
        CHECK (heavyTail.back() < 1.0e-12);
        CHECK (p.getNonFiniteEvents() == 0);
    }
    std::printf ("[TST-4] automation: %.1f s\n", seconds (start));
}

TEST_CASE ("TST-4: layout, sample rate and block size change during a session with the editor services running", "[plugin][TST-4][CH-1]")
{
    JuceEnvironment env;
    const auto noise = test::pinkNoise (1 << 16);
    SscAudioProcessor p;
    heavySetting (p, PhaseMode::NaturalPhase, Oversampling::X4, Quality::Normal);
    // A band on the centre loudspeaker only (CH-4).
    setPlain (p, bandParamId (8, BandParam::Used), 1.0f);
    setPlain (p, bandParamId (8, BandParam::Frequency), 2500.0f);
    setPlain (p, bandParamId (8, BandParam::Gain), 5.0f);
    engine::SpeakerSelection centre;
    centre.roles = engine::speakerBit (engine::SpeakerRole::Centre);
    plugin::writeSpeakers (p.getValueTreeState().state, 8, centre, nullptr);

    // As with an open editor: analyzer, meters and readouts work in the background meanwhile.
    p.getAnalyzer().addClient();
    p.getMeters().addClient();

    struct Step
    {
        S layout;
        double fs;
        int block;
        bool release;
    };
    const Step steps[] = {
        { S::stereo(), 48000.0, 512, true },
        { S::create7point1point4(), 48000.0, 256, true },
        { S::mono(), 96000.0, 64, false },
        { S::create5point1(), 44100.0, 1024, true },
        { S::create9point1point6(), 192000.0, 128, false },
        { S::discreteChannels (32), 48000.0, 32, true },
        { S::createLCR(), 384000.0, 2048, false },
        { S::stereo(), 88200.0, 8192, true },
    };
    std::int64_t position = 0;
    for (const auto& step : steps)
    {
        CAPTURE (step.layout.getDescription().toStdString(), step.fs, step.block);
        if (step.release)
            p.releaseResources();
        REQUIRE (setLayout (p, step.layout));
        p.prepareToPlay (step.fs, step.block);
        const int latency = p.getLatencySamples();

        const auto r = play (p, noise, static_cast<int> (0.2 * step.fs), BlockSizes::random (1, step.block), position);
        checkHealthy (p, r);
        CHECK (p.getLatencySamples() == latency);
        CHECK (p.getSpeakers (8) == centre); // the selection follows the session, by role

        // Channels no band selects (LFE) are the input delayed by the latency.
        const auto length = static_cast<int> (r.out[0].size());
        const auto blockStart = position - length;
        for (int ch = 0; ch < step.layout.size(); ++ch)
            if (isLfe (step.layout, ch))
                for (int i = latency; i < length; ++i)
                    REQUIRE (r.out[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)]
                             == inputSample (noise, ch, blockStart + i - latency));
    }
    p.getMeters().removeClient();
    p.getAnalyzer().removeClient();
}

TEST_CASE ("TST-4: processBlock before prepareToPlay and empty blocks are harmless", "[plugin][TST-4]")
{
    JuceEnvironment env;
    {
        SscAudioProcessor p;
        heavySetting (p, PhaseMode::NaturalPhase, Oversampling::X2, Quality::Normal);
        juce::AudioBuffer<float> buffer (2, 256);
        for (int i = 0; i < 256; ++i)
        {
            buffer.setSample (0, i, 0.1f);
            buffer.setSample (1, i, -0.1f);
        }
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi); // a host bug: left untouched
        CHECK (buffer.getSample (0, 17) == 0.1f);
        CHECK (buffer.getSample (1, 200) == -0.1f);
    }
    {
        SscAudioProcessor p;
        heavySetting (p, PhaseMode::MinimumPhase, Oversampling::Off, Quality::Normal);
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> empty (2, 0);
        juce::MidiBuffer midi;
        for (int i = 0; i < 10; ++i)
            p.processBlock (empty, midi);
        juce::AudioBuffer<double> emptyDouble (2, 0);
        p.processBlock (emptyDouble, midi);
        CHECK (p.getNonFiniteEvents() == 0);
    }
}
