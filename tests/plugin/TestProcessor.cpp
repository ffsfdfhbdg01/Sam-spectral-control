#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/ChannelLayout.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

using Catch::Approx;
using ssc::plugin::SscAudioProcessor;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

void setPlain (SscAudioProcessor& p, const juce::String& paramId, float plain)
{
    auto* param = p.getValueTreeState().getParameter (paramId);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

bool supports (SscAudioProcessor& p, const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (in);
    layout.outputBuses.add (out);
    return p.checkBusesLayoutSupported (layout);
}
} // namespace

TEST_CASE ("Processor accepts mono, stereo and surround/immersive layouts (CH-1)", "[plugin][CH-1]")
{
    JuceEnvironment env;
    SscAudioProcessor p;

    using S = juce::AudioChannelSet;
    const std::vector<S> accepted { S::mono(),
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
    for (const auto& set : accepted)
    {
        INFO (set.getDescription().toStdString());
        REQUIRE (supports (p, set, set));
    }

    REQUIRE_FALSE (supports (p, S::stereo(), S::mono()));
    REQUIRE_FALSE (supports (p, S::ambisonic (1), S::ambisonic (1)));
    REQUIRE_FALSE (supports (p, S::discreteChannels (33), S::discreteChannels (33)));
    REQUIRE_FALSE (supports (p, S::disabled(), S::disabled()));
}

TEST_CASE ("Parameter layout has stable IDs and the expected size", "[plugin][PLT-6]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    auto& state = p.getValueTreeState();

    REQUIRE (p.getParameters().size() == 11 + ssc::kNumBands * ssc::plugin::kNumBandParams);
    REQUIRE (ssc::plugin::bandParamId (0, ssc::plugin::BandParam::Frequency) == "b01_freq");
    REQUIRE (ssc::plugin::bandParamId (23, ssc::plugin::BandParam::Link) == "b24_link");
    REQUIRE (state.getParameter ("b12_threshold") != nullptr);

    // Latency-affecting parameters must not be automatable (ADR-0011).
    REQUIRE_FALSE (state.getParameter ("b05_mode")->isAutomatable());
    REQUIRE_FALSE (state.getParameter (ssc::plugin::id::phaseMode)->isAutomatable());
    REQUIRE_FALSE (state.getParameter (ssc::plugin::id::quality)->isAutomatable());
    REQUIRE_FALSE (state.getParameter (ssc::plugin::id::oversampling)->isAutomatable());
    REQUIRE (state.getParameter ("b05_freq")->isAutomatable());
}

TEST_CASE ("Bypass parameter is exposed to the host (GLB-1)", "[plugin][GLB-1]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    auto* bypass = p.getBypassParameter();
    REQUIRE (bypass != nullptr);
    REQUIRE (dynamic_cast<juce::AudioProcessorParameterWithID*> (bypass)->paramID == ssc::plugin::id::bypass);
}

TEST_CASE ("State round trip restores every parameter exactly (GLB-4)", "[plugin][GLB-4]")
{
    JuceEnvironment env;
    SscAudioProcessor source;
    std::mt19937 rng (99);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);

    // Hosts may send any normalised value; the state stores the legal (snapped) value.
    for (auto* param : source.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        REQUIRE (ranged != nullptr);
        ranged->setValueNotifyingHost (ranged->convertTo0to1 (ranged->convertFrom0to1 (dist (rng))));
    }

    juce::MemoryBlock blob;
    source.getStateInformation (blob);

    SscAudioProcessor restored;
    restored.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

    // The DSP reads the plain (denormalised) values; those must survive the round trip bit-exactly.
    const auto& a = source.getParameters();
    REQUIRE (a.size() == restored.getParameters().size());
    for (auto* param : a)
    {
        const auto& paramId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param)->paramID;
        INFO (paramId.toStdString());
        REQUIRE (source.getValueTreeState().getRawParameterValue (paramId)->load()
                 == restored.getValueTreeState().getRawParameterValue (paramId)->load());
    }
}

TEST_CASE ("State from a different plugin or a newer version is ignored", "[plugin][GLB-4]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    auto* output = p.getValueTreeState().getParameter (ssc::plugin::id::outputGain);
    output->setValueNotifyingHost (0.9f);
    const float before = output->getValue();

    juce::ValueTree foreign ("SomethingElse");
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary (*foreign.createXml(), blob);
    p.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (output->getValue() == before);

    juce::MemoryBlock good;
    p.getStateInformation (good);
    auto xml = juce::AudioProcessor::getXmlFromBinary (good.getData(), static_cast<int> (good.getSize()));
    xml->setAttribute ("version", 999);
    juce::MemoryBlock future;
    juce::AudioProcessor::copyXmlToBinary (*xml, future);

    output->setValueNotifyingHost (0.1f);
    const float changed = output->getValue();
    p.setStateInformation (future.getData(), static_cast<int> (future.getSize()));
    REQUIRE (output->getValue() == changed);
}

TEST_CASE ("Default settings pass audio through bit-exactly with zero latency (CH-6)", "[plugin][CH-6]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    REQUIRE (p.getLatencySamples() == 0);

    const auto source = ssc::test::sine (440.0, 0.5, 48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        buffer.copyFrom (ch, 0, source.data(), 512);

    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            REQUIRE (buffer.getSample (ch, i) == source[static_cast<std::size_t> (i)]);
}

TEST_CASE ("Double-precision processing is supported", "[plugin][PLT-4]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    REQUIRE (p.supportsDoublePrecisionProcessing());
    p.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    p.setPlayConfigDetails (2, 2, 96000.0, 256);
    p.prepareToPlay (96000.0, 256);

    juce::AudioBuffer<double> buffer (2, 256);
    for (int i = 0; i < 256; ++i)
    {
        buffer.setSample (0, i, std::sin (0.01 * i));
        buffer.setSample (1, i, -std::sin (0.01 * i));
    }
    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);
    REQUIRE (buffer.getSample (0, 100) == std::sin (1.0));
}

TEST_CASE ("Band parameters drive the engine; untargeted channels stay bit-exact (BAND-1, CH-3)", "[plugin][BAND-1][CH-3]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    using ssc::plugin::BandParam;
    using ssc::plugin::bandParamId;
    setPlain (p, bandParamId (4, BandParam::Used), 1.0f);
    setPlain (p, bandParamId (4, BandParam::Frequency), 1000.0f);
    setPlain (p, bandParamId (4, BandParam::Gain), 6.0f);
    setPlain (p, bandParamId (4, BandParam::Target), 1.0f); // Left

    constexpr int total = 48128;
    const auto source = ssc::test::sine (1000.0, 0.25, 48000.0, total);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    double inSum = 0.0, leftSum = 0.0;
    for (int offset = 0; offset < total; offset += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, source.data() + offset, 512);
        p.processBlock (buffer, midi);

        for (int i = 0; i < 512; ++i)
        {
            const auto x = source[static_cast<std::size_t> (offset + i)];
            REQUIRE (buffer.getSample (1, i) == x);
            if (offset >= 24000)
            {
                inSum += static_cast<double> (x) * x;
                leftSum += static_cast<double> (buffer.getSample (0, i)) * buffer.getSample (0, i);
            }
        }
    }
    REQUIRE (10.0 * std::log10 (leftSum / inSum) == Approx (6.0).margin (0.05));

    // Switching the band off returns to bit-exact pass-through after the crossfade.
    setPlain (p, bandParamId (4, BandParam::On), 0.0f);
    for (int block = 0; block < 4; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, source.data(), 512);
        p.processBlock (buffer, midi);
    }
    for (int i = 0; i < 512; ++i)
        REQUIRE (buffer.getSample (0, i) == source[static_cast<std::size_t> (i)]);
}

TEST_CASE ("Host layouts map to speaker pairs and LFE flags (CH-1, CH-4)", "[plugin][CH-1][CH-4]")
{
    using S = juce::AudioChannelSet;
    using ssc::plugin::topologyFor;

    const auto mono = topologyFor (S::mono());
    REQUIRE (mono.numChannels == 1);
    REQUIRE (mono.numPairs == 0);

    const auto stereo = topologyFor (S::stereo());
    REQUIRE (stereo.numPairs == 1);
    REQUIRE (stereo.pairs[0].left == 0);
    REQUIRE (stereo.pairs[0].right == 1);

    auto countLfe = [] (const ssc::engine::ChannelTopology& t) {
        int n = 0;
        for (int ch = 0; ch < t.numChannels; ++ch)
            n += t.isLfe (ch) ? 1 : 0;
        return n;
    };

    const auto surround51 = topologyFor (S::create5point1());
    REQUIRE (surround51.numPairs == 2); // L/R, Ls/Rs
    REQUIRE (countLfe (surround51) == 1);

    const auto atmos714 = topologyFor (S::create7point1point4());
    REQUIRE (atmos714.numChannels == 12);
    REQUIRE (atmos714.numPairs == 5); // L/R, Lss/Rss, Lrs/Rrs, Ltf/Rtf, Ltr/Rtr
    REQUIRE (countLfe (atmos714) == 1);

    const auto atmos916 = topologyFor (S::create9point1point6());
    REQUIRE (atmos916.numPairs == 7); // + Lw/Rw and Ltm/Rtm

    const auto discrete = topologyFor (S::discreteChannels (6));
    REQUIRE (discrete.numPairs == 0);
    REQUIRE (countLfe (discrete) == 0);
}
