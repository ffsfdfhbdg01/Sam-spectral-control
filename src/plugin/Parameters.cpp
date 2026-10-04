#include "plugin/Parameters.h"

#include <cmath>

namespace ssc::plugin
{
namespace
{
    constexpr int kVersion = 1;

    const char* bandSuffix (BandParam p)
    {
        switch (p)
        {
            case BandParam::Used: return "used";
            case BandParam::On: return "on";
            case BandParam::Shape: return "shape";
            case BandParam::Frequency: return "freq";
            case BandParam::Gain: return "gain";
            case BandParam::Q: return "q";
            case BandParam::Slope: return "slope";
            case BandParam::Mode: return "mode";
            case BandParam::Target: return "target";
            case BandParam::Character: return "character";
            case BandParam::AutoGain: return "auto_gain";
            case BandParam::ThresholdMode: return "threshold_mode";
            case BandParam::Threshold: return "threshold";
            case BandParam::Ratio: return "ratio";
            case BandParam::Direction: return "direction";
            case BandParam::Range: return "range";
            case BandParam::Attack: return "attack";
            case BandParam::Release: return "release";
            case BandParam::Lookahead: return "lookahead";
            case BandParam::Knee: return "knee";
            case BandParam::Mix: return "mix";
            case BandParam::Selectivity: return "selectivity";
            case BandParam::Link: return "link";
            case BandParam::Count: break;
        }
        jassertfalse;
        return "unknown";
    }

    /**
        Rounds to 5 significant digits. Applied as the legal-value snap of logarithmic parameters so
        that normalised round trips (state save/load, host automation) reproduce plain values exactly.
    */
    float roundToSignificantDigits (float value)
    {
        if (value <= 0.0f)
            return value;
        const double exponent = std::floor (std::log10 (static_cast<double> (value)));
        const double scale = std::pow (10.0, 4.0 - exponent);
        return static_cast<float> (std::round (static_cast<double> (value) * scale) / scale);
    }

    /** Logarithmic range from start to end (both > 0). */
    juce::NormalisableRange<float> logRange (float start, float end)
    {
        return { start, end,
                 [] (float s, float e, float x) {
                     return static_cast<float> (s * std::pow (static_cast<double> (e) / s, static_cast<double> (x)));
                 },
                 [] (float s, float e, float v) {
                     return static_cast<float> (std::log (static_cast<double> (v) / s) / std::log (static_cast<double> (e) / s));
                 },
                 [] (float s, float e, float v) { return juce::jlimit (s, e, roundToSignificantDigits (v)); } };
    }

    /**
        Linear range whose legal values are exact multiples of step. The snap is computed in double
        relative to zero, so it does not depend on floating-point contraction: JUCE's default snap
        (start + interval * n) becomes an FMA on arm64 and turns a 0 dB default into -5e-7 dB.
    */
    juce::NormalisableRange<float> linearRange (float start, float end, double step)
    {
        juce::NormalisableRange<float> range {
            start, end,
            [] (float s, float e, float x) { return static_cast<float> (s + (static_cast<double> (e) - s) * static_cast<double> (x)); },
            [] (float s, float e, float v) { return static_cast<float> ((static_cast<double> (v) - s) / (static_cast<double> (e) - s)); },
            [step] (float s, float e, float v) {
                return juce::jlimit (s, e, static_cast<float> (std::round (static_cast<double> (v) / step) * step));
            }
        };
        range.interval = static_cast<float> (step); // step count reported to hosts
        return range;
    }

    juce::ParameterID pid (const juce::String& name)
    {
        return { name, kVersion };
    }

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& paramId, const juce::String& name,
                                                           juce::NormalisableRange<float> range, float defaultValue,
                                                           std::function<juce::String (float)> toText, const juce::String& label = {},
                                                           std::function<float (const juce::String&)> fromText = {})
    {
        if (! fromText)
            fromText = [] (const juce::String& text) { return text.getFloatValue(); };

        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel (label)
                              .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                              .withValueFromStringFunction ([fromText] (const juce::String& text) { return fromText (text); });
        return std::make_unique<juce::AudioParameterFloat> (pid (paramId), name, range, defaultValue, attributes);
    }

    std::unique_ptr<juce::AudioParameterChoice> choiceParam (const juce::String& paramId, const juce::String& name,
                                                             const juce::StringArray& choices, int defaultIndex, bool automatable = true)
    {
        return std::make_unique<juce::AudioParameterChoice> (pid (paramId), name, choices, defaultIndex,
                                                             juce::AudioParameterChoiceAttributes().withAutomatable (automatable));
    }

    std::unique_ptr<juce::AudioParameterBool> boolParam (const juce::String& paramId, const juce::String& name, bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool> (pid (paramId), name, defaultValue);
    }

    void addBand (juce::AudioProcessorParameterGroup& group, int band)
    {
        const auto prefix = "Band " + juce::String (band + 1) + " ";
        auto bandId = [band] (BandParam p) { return bandParamId (band, p); };

        group.addChild (boolParam (bandId (BandParam::Used), prefix + "Used", false));
        group.addChild (boolParam (bandId (BandParam::On), prefix + "On", true));
        group.addChild (choiceParam (bandId (BandParam::Shape), prefix + "Shape", labels::shapes(), 0));
        group.addChild (floatParam (bandId (BandParam::Frequency), prefix + "Frequency", logRange (10.0f, 30000.0f), 1000.0f,
                                    format::frequency, {}, format::parseFrequency));
        group.addChild (floatParam (
            bandId (BandParam::Gain), prefix + "Gain", linearRange (-30.0f, 30.0f, 0.01), 0.0f,
            [] (float v) { return format::decibels (v); }, "dB"));
        group.addChild (floatParam (bandId (BandParam::Q), prefix + "Q", logRange (0.025f, 40.0f), 1.0f,
                                    [] (float v) { return juce::String (v, v < 10.0f ? 2 : 1); }));
        group.addChild (choiceParam (bandId (BandParam::Slope), prefix + "Slope", labels::slopes(), static_cast<int> (Slope::Db12)));
        // Switching to/from Spectral can change latency, so Mode is not automatable (ADR-0011).
        group.addChild (choiceParam (bandId (BandParam::Mode), prefix + "Mode", labels::bandModes(), 0, false));
        group.addChild (choiceParam (bandId (BandParam::Target), prefix + "Target", labels::targets(), 0));
        group.addChild (choiceParam (bandId (BandParam::Character), prefix + "Character", labels::characters(), 0));
        group.addChild (boolParam (bandId (BandParam::AutoGain), prefix + "Auto Gain", false));
        group.addChild (choiceParam (bandId (BandParam::ThresholdMode), prefix + "Threshold Mode", labels::thresholdModes(), 0));
        group.addChild (floatParam (
            bandId (BandParam::Threshold), prefix + "Threshold", linearRange (-80.0f, 30.0f, 0.1), -20.0f,
            [] (float v) { return format::decibels (v, false); }, "dB"));
        group.addChild (floatParam (bandId (BandParam::Ratio), prefix + "Ratio", logRange (0.1f, 100.0f), 2.0f, format::ratio));
        group.addChild (choiceParam (bandId (BandParam::Direction), prefix + "Direction", labels::directions(), 0));
        group.addChild (floatParam (
            bandId (BandParam::Range), prefix + "Range", linearRange (0.0f, 30.0f, 0.1), 12.0f,
            [] (float v) { return format::decibels (v, false); }, "dB"));
        group.addChild (floatParam (bandId (BandParam::Attack), prefix + "Attack", logRange (0.05f, 500.0f), 5.0f, format::milliseconds, {},
                                    format::parseMilliseconds));
        group.addChild (floatParam (bandId (BandParam::Release), prefix + "Release", logRange (1.0f, 5000.0f), 100.0f, format::milliseconds,
                                    {}, format::parseMilliseconds));
        group.addChild (floatParam (bandId (BandParam::Lookahead), prefix + "Lookahead", linearRange (0.0f, 20.0f, 0.01), 0.0f,
                                    format::milliseconds, {}, format::parseMilliseconds));
        group.addChild (floatParam (
            bandId (BandParam::Knee), prefix + "Knee", linearRange (0.0f, 24.0f, 0.1), 6.0f,
            [] (float v) { return format::decibels (v, false); }, "dB"));
        group.addChild (floatParam (bandId (BandParam::Mix), prefix + "Mix", linearRange (0.0f, 100.0f, 0.1), 100.0f, format::percent));
        group.addChild (
            floatParam (bandId (BandParam::Selectivity), prefix + "Selectivity", linearRange (0.0f, 100.0f, 0.1), 50.0f, format::percent));
        group.addChild (choiceParam (bandId (BandParam::Link), prefix + "Link", labels::links(), static_cast<int> (DetectorLink::Group)));
    }
} // namespace

juce::String bandParamId (int bandIndex, BandParam param)
{
    return "b" + juce::String (bandIndex + 1).paddedLeft ('0', 2) + "_" + bandSuffix (param);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");
    global->addChild (boolParam (id::bypass, "Bypass", false));
    global->addChild (floatParam (
        id::outputGain, "Output", linearRange (-24.0f, 24.0f, 0.01), 0.0f, [] (float v) { return format::decibels (v); }, "dB"));
    global->addChild (floatParam (id::mix, "Mix", linearRange (0.0f, 100.0f, 0.1), 100.0f, format::percent));
    // Latency-affecting settings are not automatable (ADR-0011).
    global->addChild (choiceParam (id::phaseMode, "Phase Mode", labels::phaseModes(), 0, false));
    global->addChild (choiceParam (id::analogModel, "Analog Model", labels::analogModels(), 0));
    global->addChild (choiceParam (id::quality, "Quality", labels::qualities(), static_cast<int> (Quality::Normal), false));
    global->addChild (choiceParam (id::oversampling, "Oversampling", labels::oversampling(), 0, false));
    global->addChild (boolParam (id::autoGain, "Auto Gain", false));
    global->addChild (choiceParam (id::autoGainWeighting, "Auto Gain Weighting", labels::autoGainWeightings(), 0));
    global->addChild (
        choiceParam (id::autoGainSpeed, "Auto Gain Speed", labels::autoGainSpeeds(), static_cast<int> (AutoGainSpeed::Stable)));
    {
        auto maxLookahead = std::make_unique<juce::AudioParameterFloat> (
            pid (id::maxLookahead), "Max Lookahead", linearRange (0.0f, 20.0f, 0.01), 0.0f,
            juce::AudioParameterFloatAttributes().withAutomatable (false).withStringFromValueFunction (
                [] (float v, int) { return format::milliseconds (v); }));
        global->addChild (std::move (maxLookahead));
    }
    // Offline render (OS-4): "Same" or its own oversampling; latency stays that of the larger one.
    global->addChild (choiceParam (id::renderOversampling, "Offline Oversampling", labels::renderOversampling(), 0, false));
    // Delta (GLB-2): monitoring only; not part of presets or A/B.
    global->addChild (boolParam (id::delta, "Delta", false));
    layout.add (std::move (global));

    for (int band = 0; band < kNumBands; ++band)
    {
        auto group =
            std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (band + 1), "Band " + juce::String (band + 1), "|");
        addBand (*group, band);
        layout.add (std::move (group));
    }

    return layout;
}

namespace labels
{
    const juce::StringArray& shapes()
    {
        static const juce::StringArray s { "Bell",  "Low Shelf", "High Shelf", "Low Cut",   "High Cut",
                                           "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass" };
        return s;
    }
    const juce::StringArray& slopes()
    {
        static const juce::StringArray s { "6 dB/oct",  "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct",
                                           "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
        return s;
    }
    const juce::StringArray& bandModes()
    {
        static const juce::StringArray s { "Static", "Dynamic", "Spectral" };
        return s;
    }
    const juce::StringArray& targets()
    {
        static const juce::StringArray s { "All", "Left", "Right", "Mid", "Side" };
        return s;
    }
    const juce::StringArray& characters()
    {
        static const juce::StringArray s { "Clear", "Warm", "Sharp" };
        return s;
    }
    const juce::StringArray& thresholdModes()
    {
        static const juce::StringArray s { "Absolute", "Relative" };
        return s;
    }
    const juce::StringArray& directions()
    {
        static const juce::StringArray s { "Above", "Below" };
        return s;
    }
    const juce::StringArray& links()
    {
        static const juce::StringArray s { "Independent", "Pair", "Group", "All" };
        return s;
    }
    const juce::StringArray& phaseModes()
    {
        static const juce::StringArray s { "Minimum Phase", "Natural Phase", "Analog Feel" };
        return s;
    }
    const juce::StringArray& analogModels()
    {
        static const juce::StringArray s { "Analog N", "Analog P", "Analog S" };
        return s;
    }
    const juce::StringArray& qualities()
    {
        static const juce::StringArray s { "Eco", "Normal", "High", "Max" };
        return s;
    }
    const juce::StringArray& oversampling()
    {
        static const juce::StringArray s { "Off", "2x", "4x", "8x" };
        return s;
    }
    const juce::StringArray& renderOversampling()
    {
        static const juce::StringArray s { "Same", "Off", "2x", "4x", "8x" };
        return s;
    }
    const juce::StringArray& autoGainWeightings()
    {
        static const juce::StringArray s { "Fletcher", "Digital" };
        return s;
    }
    const juce::StringArray& autoGainSpeeds()
    {
        static const juce::StringArray s { "Very Fast", "Fast", "Medium", "Long", "Stable" };
        return s;
    }
} // namespace labels

namespace format
{
    juce::String frequency (float hz)
    {
        if (hz >= 10000.0f)
            return juce::String (hz / 1000.0f, 1) + " kHz";
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 2) + " kHz";
        if (hz >= 100.0f)
            return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz, 1) + " Hz";
    }

    juce::String decibels (float db, bool showPlus)
    {
        const auto text = juce::String (db, 1) + " dB";
        return (showPlus && db > 0.0f) ? "+" + text : text;
    }

    juce::String ratio (float r)
    {
        if (r >= 99.99f)
            return juce::String::fromUTF8 ("\xe2\x88\x9e:1");
        return juce::String (r, r < 10.0f ? 2 : 1) + ":1";
    }

    juce::String milliseconds (float ms)
    {
        if (ms >= 1000.0f)
            return juce::String (ms / 1000.0f, 2) + " s";
        return juce::String (ms, ms < 10.0f ? 2 : 1) + " ms";
    }

    juce::String percent (float value)
    {
        return juce::String (juce::roundToInt (value)) + "%";
    }

    float parseFrequency (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const float value = t.getFloatValue();
        return t.containsChar ('k') ? value * 1000.0f : value;
    }

    float parseMilliseconds (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const float value = t.getFloatValue();
        const bool seconds = t.endsWithChar ('s') && ! t.endsWith ("ms");
        return seconds ? value * 1000.0f : value;
    }
} // namespace format

void ParameterPointers::attach (juce::AudioProcessorValueTreeState& state)
{
    auto get = [&state] (const juce::String& paramId) {
        auto* p = state.getRawParameterValue (paramId);
        jassert (p != nullptr);
        return p;
    };

    bypass = get (id::bypass);
    outputGain = get (id::outputGain);
    mix = get (id::mix);
    phaseMode = get (id::phaseMode);
    analogModel = get (id::analogModel);
    quality = get (id::quality);
    oversampling = get (id::oversampling);
    autoGain = get (id::autoGain);
    autoGainWeighting = get (id::autoGainWeighting);
    autoGainSpeed = get (id::autoGainSpeed);
    maxLookahead = get (id::maxLookahead);
    renderOversampling = get (id::renderOversampling);
    delta = get (id::delta);

    for (int band = 0; band < kNumBands; ++band)
        for (int p = 0; p < kNumBandParams; ++p)
            bands[static_cast<std::size_t> (band)][static_cast<std::size_t> (p)] = get (bandParamId (band, static_cast<BandParam> (p)));
}
} // namespace ssc::plugin
