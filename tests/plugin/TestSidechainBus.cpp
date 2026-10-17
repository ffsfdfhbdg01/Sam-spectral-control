#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "AllocationCounter.h"
#include "TestSignals.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

// The Sidechain input bus (DYN-4): optional, one or two channels. It feeds the detection of the bands set
// to External and is never part of the output.

using Catch::Approx;
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

constexpr double kFs = 48000.0;
constexpr int kBlock = 512, kLength = 48000;

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

juce::AudioProcessor::BusesLayout layoutOf (const S& main, const S& sidechain)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (main);
    layout.inputBuses.add (sidechain);
    layout.outputBuses.add (main);
    return layout;
}

/** Band 1: a Dynamic (or Spectral) bell at 1 kHz that takes up to 12 dB off. */
void keyedBand (SscAudioProcessor& p, bool external, BandMode mode = BandMode::Dynamic, int band = 0)
{
    auto set = [&p, band] (BandParam param, float value) { setPlain (p, bandParamId (band, param), value); };
    set (BandParam::Used, 1.0f);
    set (BandParam::Shape, index (Shape::Bell));
    set (BandParam::Frequency, 1000.0f);
    set (BandParam::Q, 2.0f);
    set (BandParam::Mode, index (mode));
    set (BandParam::Threshold, -40.0f);
    set (BandParam::Ratio, 4.0f);
    set (BandParam::Range, 12.0f);
    set (BandParam::Knee, 0.0f);
    set (BandParam::Selectivity, 90.0f);
    set (BandParam::Sidechain, external ? 1.0f : 0.0f);
}

double toneAmplitude (const std::vector<double>& x, double hz, std::size_t from, std::size_t length)
{
    std::complex<double> sum = 0.0;
    double windowSum = 0.0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (length));
        sum += w * x[from + i] * std::polar (1.0, -kTwoPi * hz * static_cast<double> (from + i) / kFs);
        windowSum += w;
    }
    return 2.0 * std::abs (sum) / windowSum;
}

struct Result
{
    std::vector<std::vector<double>> out; ///< [main channel][sample]
    std::size_t allocations = 0;
};

/** Plays a 1 kHz tone of amplitude 0.2 on every main channel and one of `keyAmplitude` on every
    sidechain channel, in host blocks of the full process buffer (main bus first, then the sidechain). */
template <typename Sample = float>
Result play (SscAudioProcessor& p, double keyAmplitude)
{
    const int numMain = p.getMainBusNumOutputChannels();
    const int numChannels = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    p.setRateAndBufferSizeDetails (kFs, kBlock);
    p.prepareToPlay (kFs, kBlock);

    const auto tone = test::sine (1000.0, 1.0, kFs, kLength);
    Result r;
    r.out.assign (static_cast<std::size_t> (numMain), std::vector<double> (static_cast<std::size_t> (kLength)));
    juce::AudioBuffer<Sample> buffer (numChannels, kBlock);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < kLength; offset += kBlock)
    {
        const int n = std::min (kBlock, kLength - offset);
        buffer.setSize (numChannels, n, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double amplitude = ch < p.getMainBusNumInputChannels() ? 0.2 : keyAmplitude;
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, static_cast<Sample> (amplitude * tone[static_cast<std::size_t> (offset + i)]));
        }
        {
            const test::ScopedAllocationCounter counter;
            p.processBlock (buffer, midi);
            r.allocations += counter.count();
        }
        for (int ch = 0; ch < numMain; ++ch)
            for (int i = 0; i < n; ++i)
                r.out[static_cast<std::size_t> (ch)][static_cast<std::size_t> (offset + i)] =
                    static_cast<double> (buffer.getSample (ch, i));
    }
    return r;
}

/** Level change of the 1 kHz tone in a main channel over the second half, in dB. */
double changeDb (const Result& r, int ch = 0)
{
    return gainToDb (toneAmplitude (r.out[static_cast<std::size_t> (ch)], 1000.0, kLength / 2, kLength / 2) / 0.2);
}
} // namespace

TEST_CASE ("The Sidechain input bus is optional, one or two channels, beside any main layout (DYN-4, CH-1)", "[plugin][sidechain][CH-1]")
{
    JuceEnvironment env;
    SscAudioProcessor p;

    REQUIRE (p.getBusCount (true) == 2);
    REQUIRE (p.getBusCount (false) == 1);
    const auto* bus = p.getBus (true, 1);
    REQUIRE (bus->getName() == "Sidechain");
    REQUIRE_FALSE (bus->isEnabledByDefault());
    REQUIRE_FALSE (bus->isEnabled()); // off until the host routes a signal to it

    for (const auto& main : { S::mono(), S::stereo(), S::create5point1(), S::create7point1point4(), S::discreteChannels (32) })
        for (const auto& sidechain : { S::disabled(), S::mono(), S::stereo(), S::discreteChannels (1), S::discreteChannels (2) })
        {
            CAPTURE (main.getDescription().toStdString(), sidechain.getDescription().toStdString());
            REQUIRE (p.checkBusesLayoutSupported (layoutOf (main, sidechain)));
        }
    for (const auto& sidechain : { S::createLCR(), S::create5point1(), S::discreteChannels (3) })
    {
        CAPTURE (sidechain.getDescription().toStdString());
        REQUIRE_FALSE (p.checkBusesLayoutSupported (layoutOf (S::stereo(), sidechain)));
    }
}

TEST_CASE ("A band set to External is keyed by the Sidechain bus, not by its own signal (DYN-4)", "[plugin][sidechain]")
{
    JuceEnvironment env;

    // Internal: its own tone (-14 dBFS against a -40 dBFS threshold) reaches the 12 dB range.
    {
        SscAudioProcessor p;
        REQUIRE (p.setBusesLayout (layoutOf (S::stereo(), S::stereo())));
        keyedBand (p, false);
        const auto r = play (p, 0.0);
        CHECK (changeDb (r) == Approx (-12.0).margin (0.5));
        CHECK (r.allocations == 0);
    }

    // External: a silent sidechain leaves the tone alone, a loud one takes the 12 dB off; the
    // sidechain is not heard.
    for (const auto& sidechain : { S::mono(), S::stereo() })
    {
        CAPTURE (sidechain.getDescription().toStdString());
        SscAudioProcessor p;
        REQUIRE (p.setBusesLayout (layoutOf (S::stereo(), sidechain)));
        REQUIRE (p.getTotalNumInputChannels() == 2 + sidechain.size());
        keyedBand (p, true);

        const auto quiet = play (p, 0.0);
        CHECK (changeDb (quiet, 0) == Approx (0.0).margin (0.05));
        CHECK (changeDb (quiet, 1) == Approx (0.0).margin (0.05));
        const auto keyed = play (p, 0.4);
        CHECK (changeDb (keyed, 0) == Approx (-12.0).margin (0.5));
        CHECK (changeDb (keyed, 1) == Approx (-12.0).margin (0.5));
        CHECK (quiet.allocations + keyed.allocations == 0);
        CHECK (p.getNonFiniteEvents() == 0);
    }

    // External with the bus off: the band hears silence.
    {
        SscAudioProcessor p;
        REQUIRE (p.setBusesLayout (layoutOf (S::stereo(), S::disabled())));
        keyedBand (p, true);
        CHECK (changeDb (play (p, 0.4)) == Approx (0.0).margin (0.05));
    }

    // Double precision runs the same path.
    {
        SscAudioProcessor p;
        REQUIRE (p.setBusesLayout (layoutOf (S::stereo(), S::stereo())));
        p.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        keyedBand (p, true);
        const auto keyed = play<double> (p, 0.4);
        CHECK (changeDb (keyed) == Approx (-12.0).margin (0.5));
        CHECK (keyed.allocations == 0);
    }
}

TEST_CASE ("32 main channels with a stereo sidechain: External Dynamic and Spectral bands, no allocation (DYN-4, TST-4)",
           "[plugin][sidechain][TST-4]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    REQUIRE (p.setBusesLayout (layoutOf (S::discreteChannels (32), S::stereo())));
    keyedBand (p, true, BandMode::Dynamic, 0);
    keyedBand (p, true, BandMode::Spectral, 1);
    setPlain (p, plugin::id::oversampling, index (Oversampling::X2));

    const auto r = play (p, 0.4);
    CHECK (r.allocations == 0);
    CHECK (p.getNonFiniteEvents() == 0);
    for (const auto& channel : r.out)
        CHECK (std::all_of (channel.begin(), channel.end(), [] (double y) { return std::isfinite (y) && std::abs (y) < 1.0; }));
    CHECK (changeDb (r, 0) < -6.0);
    CHECK (changeDb (r, 31) < -6.0);
}

TEST_CASE ("A state saved before the Sidechain parameter existed loads it as Internal (GLB-4)", "[plugin][sidechain][GLB-4]")
{
    JuceEnvironment env;
    SscAudioProcessor source;
    setPlain (source, bandParamId (0, BandParam::Used), 1.0f);
    setPlain (source, bandParamId (0, BandParam::Frequency), 2500.0f);
    juce::MemoryBlock blob;
    source.getStateInformation (blob);

    // The state as 1.0 saved it: no Sidechain parameters.
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (tree.getChild (i)["id"].toString().endsWith ("_sidechain"))
            tree.removeChild (i, nullptr);
    REQUIRE_FALSE (tree.getChildWithProperty ("id", bandParamId (0, BandParam::Sidechain)).isValid());
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

    // Loaded into an instance whose band 1 is on External, it comes back on Internal.
    SscAudioProcessor restored;
    setPlain (restored, bandParamId (0, BandParam::Sidechain), 1.0f);
    restored.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    auto& state = restored.getValueTreeState();
    CHECK (state.getRawParameterValue (bandParamId (0, BandParam::Sidechain))->load() == 0.0f);
    CHECK (state.getRawParameterValue (bandParamId (0, BandParam::Used))->load() == 1.0f);
    CHECK (state.getRawParameterValue (bandParamId (0, BandParam::Frequency))->load() == 2500.0f);
}
