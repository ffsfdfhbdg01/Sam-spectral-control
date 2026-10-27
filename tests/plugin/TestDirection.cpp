#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Presets.h"
#include "plugin/StateMigration.h"
#include "ui/BandModel.h"

// Direction of Dynamic and Spectral bands (DYN-5): Down cuts, Up boosts — upward compression lifts
// what stays under the threshold. Version 1.0 stored the threshold side (Above / Below) instead;
// its states, presets and copied bands keep their sound.

using Catch::Approx;
using namespace ssc;
using plugin::BandParam;
using plugin::bandParamId;
using plugin::SscAudioProcessor;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

constexpr double kFs = 48000.0;

void setPlain (SscAudioProcessor& p, const juce::String& paramId, float plain)
{
    auto* param = p.getValueTreeState().getParameter (paramId);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

float plainOf (SscAudioProcessor& p, int band, BandParam param)
{
    return p.getValueTreeState().getRawParameterValue (bandParamId (band, param))->load();
}

/** Level change in dB of a 1 kHz tone at `levelDb` through band 1 (a Dynamic bell at 1 kHz). */
double changeDb (SscAudioProcessor& p, double levelDb)
{
    p.setRateAndBufferSizeDetails (kFs, 512);
    p.prepareToPlay (kFs, 512);
    const auto tone = test::sine (1000.0, dbToGain (levelDb), kFs, 48000);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    std::vector<double> out;
    for (int offset = 0; offset < 48000; offset += 512)
    {
        const int n = std::min (512, 48000 - offset);
        buffer.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, tone[static_cast<std::size_t> (offset + i)]);
        p.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
            out.push_back (static_cast<double> (buffer.getSample (0, i)));
    }
    std::complex<double> sum = 0.0;
    double windowSum = 0.0;
    constexpr std::size_t from = 24000, length = 16384;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (length));
        sum += w * out[from + i] * std::polar (1.0, -kTwoPi * 1000.0 * static_cast<double> (from + i) / kFs);
        windowSum += w;
    }
    return gainToDb (2.0 * std::abs (sum) / windowSum / dbToGain (levelDb));
}

/** The processor's state as a tree, so that a test can turn it into a state of version 1.0. */
juce::ValueTree stateOf (SscAudioProcessor& p)
{
    juce::MemoryBlock blob;
    p.getStateInformation (blob);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (xml != nullptr);
    return juce::ValueTree::fromXml (*xml);
}

void setStored (juce::ValueTree& tree, int band, BandParam param, float value)
{
    auto child = tree.getChildWithProperty ("id", bandParamId (band, param));
    REQUIRE (child.isValid());
    child.setProperty ("value", value, nullptr);
}

/** Version 1.0 settings of four bands: Below 2:1, Above 1:2 (upward expansion), Below 1:2 (a gate,
    with times outside the swapped ranges), Above 4:1. */
void writeVersion1Bands (juce::ValueTree& tree)
{
    tree.setProperty (plugin::kVersionProperty, 1, nullptr);
    struct Old
    {
        float below, ratio, attack, release;
    };
    const Old bands[] = {
        { 1.0f, 2.0f, 5.0f, 100.0f }, { 0.0f, 0.5f, 3.0f, 50.0f }, { 1.0f, 0.5f, 0.1f, 2000.0f }, { 0.0f, 4.0f, 1.0f, 80.0f }
    };
    for (int b = 0; b < 4; ++b)
    {
        const auto& o = bands[b];
        setStored (tree, b, BandParam::Direction, o.below);
        setStored (tree, b, BandParam::Ratio, o.ratio);
        setStored (tree, b, BandParam::Attack, o.attack);
        setStored (tree, b, BandParam::Release, o.release);
    }
}

/** What version 2 must read from writeVersion1Bands(): Up, Up, Down, Down; times swapped (and
    limited) where the band acted below the threshold. */
template <typename Read>
void checkMigratedBands (Read&& read)
{
    CHECK (read (0, BandParam::Direction) == 1.0f); // upward compression
    CHECK (read (0, BandParam::Attack) == Approx (100.0f));
    CHECK (read (0, BandParam::Release) == Approx (5.0f));
    CHECK (read (1, BandParam::Direction) == 1.0f); // upward expansion
    CHECK (read (1, BandParam::Attack) == Approx (3.0f));
    CHECK (read (1, BandParam::Release) == Approx (50.0f));
    CHECK (read (2, BandParam::Direction) == 0.0f); // expansion below: a gate cuts
    CHECK (read (2, BandParam::Attack) == Approx (500.0f));
    CHECK (read (2, BandParam::Release) == Approx (1.0f));
    CHECK (read (3, BandParam::Direction) == 0.0f); // compression
    CHECK (read (3, BandParam::Attack) == Approx (1.0f));
    CHECK (read (3, BandParam::Release) == Approx (80.0f));
    for (int b = 0; b < 4; ++b)
        CHECK (read (b, BandParam::Ratio) == Approx (b == 0 ? 2.0f : b == 3 ? 4.0f : 0.5f));
}
} // namespace

TEST_CASE ("Direction Up: upward compression lifts a quiet band, Down with 1:2 cuts it like a gate (DYN-5)", "[plugin][DYN-5]")
{
    JuceEnvironment env;
    REQUIRE (plugin::labels::directions()[0] == "Down");
    REQUIRE (plugin::labels::directions()[1] == "Up");

    auto run = [] (float direction, float ratio, double levelDb) {
        SscAudioProcessor p;
        auto set = [&p] (BandParam param, float value) { setPlain (p, bandParamId (0, param), value); };
        set (BandParam::Used, 1.0f);
        set (BandParam::Mode, static_cast<float> (BandMode::Dynamic));
        set (BandParam::Q, 1.0f);
        set (BandParam::Threshold, -30.0f);
        set (BandParam::Ratio, ratio);
        set (BandParam::Direction, direction);
        set (BandParam::Range, 12.0f);
        set (BandParam::Knee, 0.0f);
        return changeDb (p, levelDb);
    };
    // Up, 4:1: a quiet tone (−50 dBFS) is lifted by the Range, a loud one (−10 dBFS) is left alone.
    CHECK (run (1.0f, 4.0f, -50.0) == Approx (12.0).margin (0.3));
    CHECK (run (1.0f, 4.0f, -10.0) == Approx (0.0).margin (0.1));
    // Down, 4:1: compression of the loud tone only.
    CHECK (run (0.0f, 4.0f, -50.0) == Approx (0.0).margin (0.1));
    CHECK (run (0.0f, 4.0f, -10.0) == Approx (-12.0).margin (0.3));
    // Down, 1:2: expansion below the threshold; Up, 1:2: upward expansion above it.
    CHECK (run (0.0f, 0.5f, -50.0) == Approx (-12.0).margin (0.3));
    CHECK (run (0.0f, 0.5f, -10.0) == Approx (0.0).margin (0.1));
    CHECK (run (1.0f, 0.5f, -50.0) == Approx (0.0).margin (0.1));
    CHECK (run (1.0f, 0.5f, -10.0) == Approx (12.0).margin (0.3));
}

TEST_CASE ("A 1.0 state keeps its sound: Direction becomes Down or Up, attack and release follow (GLB-4)", "[plugin][GLB-4][DYN-5]")
{
    JuceEnvironment env;
    SscAudioProcessor source;
    auto tree = stateOf (source);
    writeVersion1Bands (tree);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);

    SscAudioProcessor restored;
    restored.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    checkMigratedBands ([&restored] (int b, BandParam p) { return plainOf (restored, b, p); });
    CHECK (static_cast<int> (stateOf (restored)[plugin::kVersionProperty]) == plugin::kStateVersion);

    // Saved again, it reads the same (version 2 is not migrated twice).
    juce::MemoryBlock again;
    restored.getStateInformation (again);
    SscAudioProcessor second;
    second.setStateInformation (again.getData(), static_cast<int> (again.getSize()));
    checkMigratedBands ([&second] (int b, BandParam p) { return plainOf (second, b, p); });
}

TEST_CASE ("A 1.0 preset and a band copied by 1.0 keep their sound too (GLB-3, DYN-5)", "[plugin][GLB-3][DYN-5]")
{
    JuceEnvironment env;
    SscAudioProcessor p;
    auto& state = p.getValueTreeState();

    auto preset = plugin::capturePreset (state, "Old");
    writeVersion1Bands (preset);
    REQUIRE (plugin::applyPreset (state, preset));
    checkMigratedBands ([&p] (int b, BandParam param) { return plainOf (p, b, param); });

    // A band text of 1.0: version-1 prefix, the parameters before Sidechain, the speakers.
    ui::BandModel model (state, p.getUndoManager());
    const int band = model.createBand (Shape::Bell, 1000.0f, 0.0f);
    auto text = model.bandText (band);
    juce::StringArray items;
    items.addTokens (text.fromFirstOccurrenceOf (":", false, false), ",", {});
    items.remove (static_cast<int> (BandParam::Sidechain));
    auto normalised = [&model, band] (BandParam param, float plain) {
        return juce::String (model.parameter (band, param).convertTo0to1 (plain), 9);
    };
    items.set (static_cast<int> (BandParam::Direction), normalised (BandParam::Direction, 1.0f)); // Below
    items.set (static_cast<int> (BandParam::Ratio), normalised (BandParam::Ratio, 2.0f));
    items.set (static_cast<int> (BandParam::Attack), normalised (BandParam::Attack, 5.0f));
    items.set (static_cast<int> (BandParam::Release), normalised (BandParam::Release, 100.0f));
    REQUIRE (model.pasteBandText (band, "SamSpectralControl.band.v1:" + items.joinIntoString (",")));
    CHECK (plainOf (p, band, BandParam::Direction) == 1.0f);
    CHECK (plainOf (p, band, BandParam::Attack) == Approx (100.0f).epsilon (1.0e-3));
    CHECK (plainOf (p, band, BandParam::Release) == Approx (5.0f).epsilon (1.0e-3));

    // A text of this version is taken as it is.
    setPlain (p, bandParamId (band, BandParam::Direction), 0.0f);
    REQUIRE (model.pasteBandText (band, text));
    CHECK (plainOf (p, band, BandParam::Direction) == 0.0f);
    CHECK (plainOf (p, band, BandParam::Attack) == Approx (5.0f).epsilon (1.0e-3));
}

TEST_CASE ("Direction migration in plain units: flips where the ratio expands, swaps the times below the threshold", "[plugin][DYN-5]")
{
    using plugin::BandDirection;
    using plugin::migrateBandDirection;
    auto check = [] (BandDirection old, float direction, float attack, float release) {
        const auto now = migrateBandDirection (old);
        CHECK (now.direction == direction);
        CHECK (now.ratio == old.ratio);
        CHECK (now.attackMs == Approx (attack));
        CHECK (now.releaseMs == Approx (release));
    };
    check ({ 0.0f, 2.0f, 5.0f, 100.0f }, 0.0f, 5.0f, 100.0f);   // Above 2:1 → Down
    check ({ 1.0f, 2.0f, 5.0f, 100.0f }, 1.0f, 100.0f, 5.0f);   // Below 2:1 → Up, times swapped
    check ({ 0.0f, 0.5f, 5.0f, 100.0f }, 1.0f, 5.0f, 100.0f);   // Above 1:2 → Up
    check ({ 1.0f, 0.5f, 5.0f, 100.0f }, 0.0f, 100.0f, 5.0f);   // Below 1:2 → Down, times swapped
    check ({ 1.0f, 1.0f, 0.05f, 5000.0f }, 1.0f, 500.0f, 1.0f); // limited to the ranges
}
