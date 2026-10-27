#include "plugin/Parameters.h"
#include "plugin/Presets.h"
#include "plugin/SpeakerState.h"
#include "plugin/StateMigration.h"

namespace ssc::plugin
{
namespace
{
    /** One band of a factory preset; the dynamics fields matter in Dynamic and Spectral mode. */
    struct Band
    {
        Shape shape = Shape::Bell;
        float hz = 1000.0f;
        float gainDb = 0.0f;
        float q = 0.7071f;
        Slope slope = Slope::Db12;
        BandMode mode = BandMode::Static;
        StereoComponent target = StereoComponent::All;
        Character character = Character::Clear;
        ThresholdMode thresholdMode = ThresholdMode::Absolute;
        float thresholdDb = -20.0f;
        float ratio = 2.0f;
        bool upward = false; ///< Direction Up (boost) instead of Down (cut)
        float rangeDb = 12.0f;
        float attackMs = 5.0f;
        float releaseMs = 100.0f;
        float kneeDb = 6.0f;
        float selectivity = 50.0f;
        std::uint64_t speakerRoles = 0; ///< 0: the default selection
    };

    class Builder
    {
    public:
        Builder (const juce::String& name, const juce::String& category)
            : tree (kPresetType)
        {
            tree.setProperty (kVersionProperty, kStateVersion, nullptr);
            tree.setProperty ("name", name, nullptr);
            tree.setProperty ("category", category, nullptr);
        }

        Builder& global (const char* paramId, float value)
        {
            set (paramId, value);
            return *this;
        }

        Builder& band (const Band& b)
        {
            const int index = next++;
            auto param = [index] (BandParam p) { return bandParamId (index, p); };
            set (param (BandParam::Used), 1.0f);
            set (param (BandParam::Shape), static_cast<float> (b.shape));
            set (param (BandParam::Frequency), b.hz);
            set (param (BandParam::Gain), b.gainDb);
            set (param (BandParam::Q), b.q);
            set (param (BandParam::Slope), static_cast<float> (b.slope));
            set (param (BandParam::Mode), static_cast<float> (b.mode));
            set (param (BandParam::Target), static_cast<float> (b.target));
            set (param (BandParam::Character), static_cast<float> (b.character));
            if (b.mode != BandMode::Static)
            {
                set (param (BandParam::ThresholdMode), static_cast<float> (b.thresholdMode));
                set (param (BandParam::Threshold), b.thresholdDb);
                set (param (BandParam::Ratio), b.ratio);
                set (param (BandParam::Direction), b.upward ? 1.0f : 0.0f);
                set (param (BandParam::Range), b.rangeDb);
                set (param (BandParam::Attack), b.attackMs);
                set (param (BandParam::Release), b.releaseMs);
                set (param (BandParam::Knee), b.kneeDb);
                set (param (BandParam::Selectivity), b.selectivity);
            }
            if (b.speakerRoles != 0)
            {
                auto speakers = tree.getOrCreateChildWithName (kSpeakersType, nullptr);
                engine::SpeakerSelection selection;
                selection.roles = b.speakerRoles;
                speakers.setProperty (speakerPropertyFor (index), speakerSelectionToString (selection), nullptr);
            }
            return *this;
        }

        FactoryPreset build() const { return { tree["name"].toString(), tree["category"].toString(), tree.createCopy() }; }

    private:
        void set (const juce::String& paramId, float value)
        {
            juce::ValueTree param ("PARAM");
            param.setProperty ("id", paramId, nullptr);
            param.setProperty ("value", value, nullptr);
            tree.appendChild (param, nullptr);
        }

        juce::ValueTree tree;
        int next = 0;
    };

    Band cut (Shape shape, float hz, Slope slope, StereoComponent target = StereoComponent::All)
    {
        Band b;
        b.shape = shape;
        b.hz = hz;
        b.slope = slope;
        b.target = target;
        return b;
    }

    Band bell (float hz, float gainDb, float q, Character character = Character::Clear)
    {
        Band b;
        b.hz = hz;
        b.gainDb = gainDb;
        b.q = q;
        b.character = character;
        return b;
    }

    Band shelf (Shape shape, float hz, float gainDb, Character character = Character::Clear, StereoComponent target = StereoComponent::All)
    {
        Band b;
        b.shape = shape;
        b.hz = hz;
        b.gainDb = gainDb;
        b.character = character;
        b.target = target;
        return b;
    }

    Band dynamicBell (float hz, float q, float thresholdDb, float ratio, float rangeDb, float attackMs, float releaseMs)
    {
        Band b;
        b.hz = hz;
        b.q = q;
        b.mode = BandMode::Dynamic;
        b.thresholdDb = thresholdDb;
        b.ratio = ratio;
        b.rangeDb = rangeDb;
        b.attackMs = attackMs;
        b.releaseMs = releaseMs;
        return b;
    }

    Band spectralBell (float hz, float q, float relativeDb, float rangeDb, float selectivity)
    {
        Band b;
        b.hz = hz;
        b.q = q;
        b.mode = BandMode::Spectral;
        b.thresholdMode = ThresholdMode::Relative;
        b.thresholdDb = relativeDb;
        b.ratio = 3.0f;
        b.rangeDb = rangeDb;
        b.attackMs = 5.0f;
        b.releaseMs = 80.0f;
        b.selectivity = selectivity;
        return b;
    }

    Band notch (float hz, float q)
    {
        Band b;
        b.shape = Shape::Notch;
        b.hz = hz;
        b.q = q;
        return b;
    }

    std::vector<FactoryPreset> makeFactoryPresets()
    {
        std::vector<FactoryPreset> presets;
        auto add = [&presets] (const Builder& b) { presets.push_back (b.build()); };

        add (Builder ("Default", "General"));

        add (Builder ("Vocal - Clarity", "Vocal")
                 .band (cut (Shape::LowCut, 80.0f, Slope::Db24))
                 .band (bell (250.0f, -2.0f, 1.2f))
                 .band (bell (3000.0f, 2.0f, 0.9f))
                 .band (shelf (Shape::HighShelf, 10000.0f, 2.5f)));

        {
            auto deEss = dynamicBell (6500.0f, 2.5f, -30.0f, 4.0f, 8.0f, 1.0f, 60.0f);
            add (Builder ("Vocal - De-ess (Dynamic)", "Vocal").band (cut (Shape::LowCut, 70.0f, Slope::Db18)).band (deEss));
        }

        add (Builder ("Vocal - Resonance Tamer (Spectral)", "Vocal")
                 .global (id::autoGain, 1.0f)
                 .band (cut (Shape::LowCut, 80.0f, Slope::Db24))
                 .band (spectralBell (2500.0f, 0.4f, 6.0f, 6.0f, 70.0f)));

        add (Builder ("Kick - Tight Low End", "Drums")
                 .band (bell (55.0f, 3.0f, 1.2f))
                 .band (dynamicBell (220.0f, 2.0f, -24.0f, 3.0f, 6.0f, 3.0f, 80.0f))
                 .band (bell (3500.0f, 2.0f, 1.5f)));

        add (Builder ("Snare - Body and Snap", "Drums")
                 .band (bell (200.0f, 2.0f, 1.4f))
                 .band (bell (900.0f, -2.5f, 2.5f))
                 .band (shelf (Shape::HighShelf, 8000.0f, 2.0f)));

        add (Builder ("Acoustic Guitar - Boom Control", "Instruments")
                 .band (cut (Shape::LowCut, 70.0f, Slope::Db18))
                 .band (dynamicBell (160.0f, 1.5f, -28.0f, 3.0f, 6.0f, 10.0f, 150.0f))
                 .band (shelf (Shape::HighShelf, 9000.0f, 1.5f)));

        add (Builder ("Mix Bus - Gentle Tilt (Natural Phase)", "Mix")
                 .global (id::phaseMode, static_cast<float> (PhaseMode::NaturalPhase))
                 .band (shelf (Shape::LowShelf, 120.0f, 1.0f))
                 .band (shelf (Shape::HighShelf, 9000.0f, 1.5f)));

        add (Builder ("Stereo - Side Air, Mono Bass", "Mix")
                 .band (cut (Shape::LowCut, 150.0f, Slope::Db12, StereoComponent::Side))
                 .band (shelf (Shape::HighShelf, 9000.0f, 2.5f, Character::Clear, StereoComponent::Side)));

        add (Builder ("Mastering - Analog N Warmth", "Mastering")
                 .global (id::phaseMode, static_cast<float> (PhaseMode::AnalogFeel))
                 .global (id::analogModel, static_cast<float> (AnalogModel::N))
                 .global (id::quality, static_cast<float> (Quality::High))
                 .band (shelf (Shape::LowShelf, 80.0f, 1.0f, Character::Warm))
                 .band (bell (320.0f, -0.8f, 0.8f))
                 .band (shelf (Shape::HighShelf, 12000.0f, 1.2f)));

        add (Builder ("Mastering - Analog P Low-End Trick", "Mastering")
                 .global (id::phaseMode, static_cast<float> (PhaseMode::AnalogFeel))
                 .global (id::analogModel, static_cast<float> (AnalogModel::P))
                 .global (id::quality, static_cast<float> (Quality::High))
                 .band (shelf (Shape::LowShelf, 60.0f, 3.0f))
                 .band (shelf (Shape::LowShelf, 60.0f, -2.0f))
                 .band (shelf (Shape::HighShelf, 12000.0f, 1.5f, Character::Warm)));

        add (Builder ("Mastering - Resonance Control (Spectral)", "Mastering")
                 .global (id::autoGain, 1.0f)
                 .band (spectralBell (1000.0f, 0.3f, 6.0f, 4.0f, 60.0f)));

        {
            auto presence = bell (2800.0f, 2.0f, 1.0f);
            presence.speakerRoles = engine::speakerBit (engine::SpeakerRole::Centre);
            auto rumble = cut (Shape::LowCut, 100.0f, Slope::Db24);
            rumble.speakerRoles = engine::speakerBit (engine::SpeakerRole::Centre);
            add (Builder ("Dialogue - Centre Clarity (Surround)", "Post").band (rumble).band (presence));
        }

        for (const float mains : { 50.0f, 60.0f })
        {
            Builder hum ("Hum Removal " + juce::String (juce::roundToInt (mains)) + " Hz", "Repair");
            for (int harmonic = 1; harmonic <= 5; ++harmonic)
                hum.band (notch (mains * static_cast<float> (harmonic), 30.0f));
            add (hum);
        }
        return presets;
    }
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const auto presets = makeFactoryPresets();
    return presets;
}
} // namespace ssc::plugin
