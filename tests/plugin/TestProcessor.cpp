#include <catch2/catch_test_macros.hpp>

#include <random>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

using ssc::plugin::SscAudioProcessor;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

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
    const std::vector<S> accepted { S::mono(), S::stereo(), S::createLCR(), S::createLCRS(), S::quadraphonic(),
                                    S::create5point0(), S::create5point1(), S::create6point1(), S::create7point0(),
                                    S::create7point1(), S::create5point1point2(), S::create5point1point4(),
                                    S::create7point1point2(), S::create7point1point4(), S::create7point1point6(),
                                    S::create9point1point4(), S::create9point1point6(), S::discreteChannels (1),
                                    S::discreteChannels (24), S::discreteChannels (32) };
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
