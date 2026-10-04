#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Presets.h"
#include "plugin/SpeakerState.h"
#include "plugin/StateMigration.h"

using Catch::Approx;
using namespace ssc;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

void setParam (plugin::SscAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getValueTreeState().getParameter (id);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float getParam (plugin::SscAudioProcessor& p, const juce::String& id)
{
    auto* param = p.getValueTreeState().getParameter (id);
    REQUIRE (param != nullptr);
    return param->convertFrom0to1 (param->getValue());
}

/** A non-default setting: two bands, a global, speakers. */
void configure (plugin::SscAudioProcessor& p)
{
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Used), 1.0f);
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Frequency), 2500.0f);
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Gain), 4.5f);
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Character), static_cast<float> (Character::Warm));
    setParam (p, plugin::bandParamId (5, plugin::BandParam::Used), 1.0f);
    setParam (p, plugin::bandParamId (5, plugin::BandParam::Mode), static_cast<float> (BandMode::Dynamic));
    setParam (p, plugin::id::phaseMode, static_cast<float> (PhaseMode::NaturalPhase));
    setParam (p, plugin::id::outputGain, -3.0f);
    engine::SpeakerSelection centre;
    centre.roles = engine::speakerBit (engine::SpeakerRole::Centre);
    plugin::writeSpeakers (p.getValueTreeState().state, 0, centre, nullptr);
}

/** Every parameter (but Bypass) and the speakers, in plain values. */
std::map<juce::String, float> snapshot (plugin::SscAudioProcessor& p)
{
    std::map<juce::String, float> values;
    for (auto* param : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            ranged != nullptr && ranged->getParameterID() != plugin::id::bypass)
            values[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return values;
}

bool sameValues (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b)
{
    if (a.size() != b.size())
        return false;
    for (const auto& [id, value] : a)
        if (std::abs (value - b.at (id)) > 1.0e-4f * std::max (1.0f, std::abs (value)))
            return false;
    return true;
}
} // namespace

TEST_CASE ("A preset holds every parameter but Bypass and the speakers (GLB-3)", "[plugin][GLB-3]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor a;
    configure (a);
    setParam (a, plugin::id::bypass, 1.0f);
    const auto preset = plugin::capturePreset (a.getValueTreeState(), "Test", "Tests");
    CHECK (plugin::presetName (preset) == "Test");
    CHECK (preset.getChildWithProperty ("id", plugin::id::bypass).isValid() == false);

    plugin::SscAudioProcessor b;
    REQUIRE (plugin::applyPreset (b.getValueTreeState(), preset));
    CHECK (sameValues (snapshot (a), snapshot (b)));
    CHECK (getParam (b, plugin::id::bypass) == 0.0f); // Bypass stays as it was
    CHECK (plugin::readSpeakers (b.getValueTreeState().state, 0).roles == engine::speakerBit (engine::SpeakerRole::Centre));
    CHECK (b.getSpeakers (0).roles == engine::speakerBit (engine::SpeakerRole::Centre)); // the audio thread's copy follows

    // A preset without some parameters sets them to their defaults; unknown IDs are ignored.
    auto partial = preset.createCopy();
    partial.removeChild (partial.getChildWithProperty ("id", plugin::id::outputGain), nullptr);
    juce::ValueTree unknown ("PARAM");
    unknown.setProperty ("id", "no_such_parameter", nullptr);
    unknown.setProperty ("value", 5.0, nullptr);
    partial.appendChild (unknown, nullptr);
    REQUIRE (plugin::applyPreset (b.getValueTreeState(), partial));
    CHECK (getParam (b, plugin::id::outputGain) == Approx (0.0f));

    // Not a preset, or from a newer version: nothing changes.
    auto newer = preset.createCopy();
    newer.setProperty (plugin::kVersionProperty, plugin::kStateVersion + 1, nullptr);
    plugin::SscAudioProcessor c;
    const auto before = snapshot (c);
    CHECK_FALSE (plugin::applyPreset (c.getValueTreeState(), newer));
    CHECK_FALSE (plugin::applyPreset (c.getValueTreeState(), juce::ValueTree ("Something")));
    CHECK (sameValues (before, snapshot (c)));
}

TEST_CASE ("Loading a preset is one undo step (GLB-3)", "[plugin][GLB-3]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor p;
    configure (p);
    const auto configured = snapshot (p);
    auto& undo = p.getUndoManager();
    undo.clearUndoHistory();

    const auto& factory = plugin::factoryPresets();
    REQUIRE (factory.size() >= 10);
    const auto& vocal = factory[1];
    REQUIRE (plugin::loadPreset (p.getValueTreeState(), undo, vocal.tree));
    CHECK (p.getValueTreeState().state[plugin::kPresetName].toString() == vocal.name);
    CHECK_FALSE (sameValues (configured, snapshot (p)));
    REQUIRE (undo.undo());
    CHECK (sameValues (configured, snapshot (p)));
    REQUIRE (undo.redo());
    CHECK (p.getValueTreeState().state[plugin::kPresetName].toString() == vocal.name);
}

TEST_CASE ("Factory presets load; Default restores every default (GLB-3)", "[plugin][GLB-3]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor fresh;
    const auto defaults = snapshot (fresh);

    juce::StringArray names;
    for (const auto& preset : plugin::factoryPresets())
    {
        CAPTURE (preset.name.toStdString());
        CHECK (preset.name.isNotEmpty());
        CHECK (preset.category.isNotEmpty());
        CHECK_FALSE (names.contains (preset.name));
        names.add (preset.name);

        plugin::SscAudioProcessor p;
        configure (p);
        REQUIRE (plugin::applyPreset (p.getValueTreeState(), preset.tree));
        if (preset.name == "Default")
            CHECK (sameValues (defaults, snapshot (p)));
        else
            CHECK (getParam (p, plugin::bandParamId (0, plugin::BandParam::Used)) == 1.0f);

        // Every preset runs: finite output at 48 kHz stereo.
        p.setPlayConfigDetails (2, 2, 48000.0, 256);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        juce::Random random (7);
        bool finite = true;
        for (int block = 0; block < 40; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    buffer.setSample (ch, i, 0.25f * (random.nextFloat() * 2.0f - 1.0f));
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));
        }
        CHECK (finite);
        p.releaseResources();
    }
}

TEST_CASE ("User presets: save, list, read back; files carry the state version (GLB-3)", "[plugin][GLB-3]")
{
    JuceEnvironment env;
    const auto folder = juce::File::createTempFile ("ssc-presets");
    plugin::PresetLibrary library (folder);
    plugin::SscAudioProcessor p;
    configure (p);
    const auto file = library.save (plugin::capturePreset (p.getValueTreeState()), "My Vocal / Bright");
    REQUIRE (file.existsAsFile());
    CHECK (file.getFileName().endsWith (plugin::kPresetExtension));
    library.save (plugin::capturePreset (p.getValueTreeState()), "Another");
    const auto files = library.list();
    REQUIRE (files.size() == 2);
    CHECK (files[0].getFileNameWithoutExtension() == "Another");

    const auto read = plugin::readPresetFile (file);
    REQUIRE (read.isValid());
    CHECK (plugin::presetName (read) == "My Vocal / Bright");
    CHECK (static_cast<int> (read[plugin::kVersionProperty]) == plugin::kStateVersion);
    plugin::SscAudioProcessor q;
    REQUIRE (plugin::applyPreset (q.getValueTreeState(), read));
    CHECK (sameValues (snapshot (p), snapshot (q)));

    // Garbage and newer versions are refused.
    const auto garbage = folder.getChildFile ("broken.sscpreset");
    garbage.replaceWithText ("<not xml");
    CHECK_FALSE (plugin::readPresetFile (garbage).isValid());
    folder.deleteRecursively();
}

TEST_CASE ("A/B: two settings, switching and copying are undoable and saved with the state (GLB-3)", "[plugin][GLB-3]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor p;
    auto& s = p.getValueTreeState();
    auto& undo = p.getUndoManager();
    configure (p);
    const auto a = snapshot (p);
    CHECK (plugin::compareSlot (s.state) == 0);

    // The first switch starts B as a copy of A.
    plugin::switchCompareSlot (s, undo);
    CHECK (plugin::compareSlot (s.state) == 1);
    CHECK (sameValues (a, snapshot (p)));
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Gain), -6.0f);
    setParam (p, plugin::id::phaseMode, static_cast<float> (PhaseMode::MinimumPhase));
    const auto b = snapshot (p);

    plugin::switchCompareSlot (s, undo);
    CHECK (plugin::compareSlot (s.state) == 0);
    CHECK (sameValues (a, snapshot (p)));
    plugin::switchCompareSlot (s, undo);
    CHECK (sameValues (b, snapshot (p)));

    // Undo of a switch shows the other slot again.
    REQUIRE (undo.undo());
    CHECK (plugin::compareSlot (s.state) == 0);
    CHECK (sameValues (a, snapshot (p)));

    // Saved with the session: the slot shown and the other one.
    juce::MemoryBlock data;
    p.getStateInformation (data);
    plugin::SscAudioProcessor restored;
    restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    CHECK (plugin::compareSlot (restored.getValueTreeState().state) == 0);
    CHECK (sameValues (a, snapshot (restored)));
    plugin::switchCompareSlot (restored.getValueTreeState(), restored.getUndoManager());
    CHECK (sameValues (b, snapshot (restored)));

    // Copy A to B, undoable.
    plugin::copyToOtherSlot (s, undo);
    plugin::switchCompareSlot (s, undo);
    CHECK (sameValues (a, snapshot (p)));
    REQUIRE (undo.undo()); // the switch
    REQUIRE (undo.undo()); // the copy
    plugin::switchCompareSlot (s, undo);
    CHECK (sameValues (b, snapshot (p)));
}
