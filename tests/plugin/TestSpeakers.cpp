#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/ChannelLayout.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/SpeakerState.h"
#include "ui/BandModel.h"
#include "ui/SpeakerPanel.h"

using namespace ssc;
using engine::SpeakerGroup;
using engine::SpeakerRole;
using engine::SpeakerSelection;
using plugin::SscAudioProcessor;

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

SpeakerSelection sideSurrounds()
{
    SpeakerSelection s;
    s.roles = engine::speakerBit (SpeakerRole::LeftSurroundSide) | engine::speakerBit (SpeakerRole::RightSurroundSide);
    return s;
}
} // namespace

TEST_CASE ("Speaker selections are saved with the state (CH-4, GLB-4)", "[plugin][CH-4]")
{
    JuceEnvironment env;
    SscAudioProcessor a;
    plugin::writeSpeakers (a.getValueTreeState().state, 3, sideSurrounds(), nullptr);
    REQUIRE (a.getSpeakers (3) == sideSurrounds()); // the audio-thread copy follows the tree

    juce::MemoryBlock data;
    a.getStateInformation (data);
    SscAudioProcessor b;
    b.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    REQUIRE (b.getSpeakers (3) == sideSurrounds());
    REQUIRE (b.getSpeakers (0).isDefault());

    // The default leaves no trace; malformed text falls back to the default.
    plugin::writeSpeakers (a.getValueTreeState().state, 3, {}, nullptr);
    REQUIRE_FALSE (a.getValueTreeState().state.getChildWithName (plugin::kSpeakersType).hasProperty (plugin::speakerPropertyFor (3)));
    REQUIRE (a.getSpeakers (3).isDefault());
    REQUIRE (plugin::speakerSelectionFromString (plugin::speakerSelectionToString (sideSurrounds())) == sideSurrounds());
    REQUIRE (plugin::speakerSelectionFromString ("not:hex!").isDefault());
}

TEST_CASE ("Speaker changes are one undo step and are reset with the band (CH-4)", "[ui][CH-4]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    ui::BandModel model (p.getValueTreeState(), p.getUndoManager());
    auto& undo = p.getUndoManager();

    const int band = model.createBand (Shape::Bell, 1000.0f, -3.0f);
    REQUIRE (band >= 0);
    const auto fronts = ui::SpeakerPanel::withGroups ({}, { SpeakerGroup::Front, SpeakerGroup::Centre, SpeakerGroup::Wides });
    {
        ui::BandModel::ScopedEdit edit (model, "Speakers");
        model.setSpeakers (band, fronts);
    }
    REQUIRE (model.speakers (band) == fronts);
    REQUIRE (p.getSpeakers (band) == fronts);
    REQUIRE (model.band (band).speakers == fronts);

    REQUIRE (undo.undo());
    REQUIRE (model.speakers (band).isDefault());
    REQUIRE (undo.redo());
    REQUIRE (model.speakers (band) == fronts);

    // Deleting the band resets its speakers (a new band in the slot starts with All); undo restores.
    model.deleteBand (band);
    REQUIRE (model.speakers (band).isDefault());
    REQUIRE (undo.undo());
    REQUIRE (model.speakers (band) == fronts);
}

TEST_CASE ("Speaker panel quick selections and summary (CH-4)", "[ui][CH-4]")
{
    const auto layout = juce::AudioChannelSet::create7point1point4();
    const auto topology = plugin::topologyFor (layout);
    REQUIRE (topology.numChannels == 12);

    using ui::SpeakerPanel;
    REQUIRE (SpeakerPanel::summary ({}, topology) == "All");
    SpeakerSelection withLfe;
    withLfe.roles |= SpeakerSelection::kLfeRoles;
    REQUIRE (SpeakerPanel::summary (withLfe, topology) == "All + LFE");
    REQUIRE (SpeakerPanel::summary ({ 0, 0 }, topology) == "None");
    REQUIRE (SpeakerPanel::summary (sideSurrounds(), topology) == "2/12");

    const auto tops = SpeakerPanel::withGroups ({}, { SpeakerGroup::TopFront, SpeakerGroup::TopMiddle, SpeakerGroup::TopRear });
    REQUIRE (tops.count (topology) == 4);
    const auto bed = SpeakerPanel::withGroups (
        {}, { SpeakerGroup::Front, SpeakerGroup::Centre, SpeakerGroup::SideSurround, SpeakerGroup::RearSurround, SpeakerGroup::Wides });
    REQUIRE (bed.count (topology) == 7);
    REQUIRE_FALSE (bed.includesLfe());
    REQUIRE (SpeakerPanel::withGroups (withLfe, { SpeakerGroup::TopFront }).includesLfe()); // LFE state is kept

    const int lss = layout.getChannelIndexForType (juce::AudioChannelSet::leftSurroundSide);
    REQUIRE (lss >= 0);
    const auto toggled = SpeakerPanel::toggled ({}, topology, lss);
    REQUIRE_FALSE (toggled.selects (topology, lss));
    REQUIRE (toggled.count (topology) == 10);
    REQUIRE (SpeakerPanel::toggled (toggled, topology, lss).isDefault());
}

TEST_CASE ("7.1.4: speakers outside the band stay bit-exact with oversampling, Natural Phase and Auto Gain (CH-6, CH-4)",
           "[plugin][CH-6][CH-4]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    const auto layout = juce::AudioChannelSet::create7point1point4();
    juce::AudioProcessor::BusesLayout buses;
    buses.inputBuses.add (layout);
    buses.outputBuses.add (layout);
    REQUIRE (p.setBusesLayout (buses));
    p.setPlayConfigDetails (12, 12, 48000.0, 512);

    using plugin::BandParam;
    using plugin::bandParamId;
    setPlain (p, plugin::id::phaseMode, static_cast<float> (PhaseMode::NaturalPhase));
    setPlain (p, plugin::id::oversampling, static_cast<float> (Oversampling::X2));
    setPlain (p, plugin::id::autoGain, 1.0f);
    setPlain (p, bandParamId (0, BandParam::Used), 1.0f);
    setPlain (p, bandParamId (0, BandParam::Frequency), 2000.0f);
    setPlain (p, bandParamId (0, BandParam::Gain), -6.0f);
    setPlain (p, bandParamId (0, BandParam::Target), static_cast<float> (StereoComponent::Side));
    plugin::writeSpeakers (p.getValueTreeState().state, 0, sideSurrounds(), nullptr);
    p.prepareToPlay (48000.0, 512);
    const auto latency = static_cast<std::size_t> (p.getLatencySamples());
    REQUIRE (latency > 0);

    constexpr int total = 16384;
    std::vector<std::vector<float>> input, output (12);
    for (int ch = 0; ch < 12; ++ch)
    {
        auto noise = ssc::test::pinkNoise (total, 100u + static_cast<unsigned> (ch));
        input.push_back (std::move (noise));
    }
    juce::AudioBuffer<float> buffer (12, 512);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < total; offset += 512)
    {
        for (int ch = 0; ch < 12; ++ch)
            buffer.copyFrom (ch, 0, input[static_cast<std::size_t> (ch)].data() + offset, 512);
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 12; ++ch)
            output[static_cast<std::size_t> (ch)].insert (output[static_cast<std::size_t> (ch)].end(), buffer.getReadPointer (ch),
                                                          buffer.getReadPointer (ch) + 512);
    }

    const int lss = layout.getChannelIndexForType (juce::AudioChannelSet::leftSurroundSide);
    const int rss = layout.getChannelIndexForType (juce::AudioChannelSet::rightSurroundSide);
    for (int ch = 0; ch < 12; ++ch)
    {
        CAPTURE (ch);
        const auto& in = input[static_cast<std::size_t> (ch)];
        const auto& out = output[static_cast<std::size_t> (ch)];
        bool exact = true;
        for (std::size_t i = latency; i < out.size(); ++i)
            exact = exact && out[i] == in[i - latency];
        CHECK (exact == (ch != lss && ch != rss));
    }
}
