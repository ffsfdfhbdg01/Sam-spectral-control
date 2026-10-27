#include "ui/BandModel.h"

#include "plugin/SpeakerState.h"
#include "plugin/StateMigration.h"

#include <utility>

#include "ui/BandLogic.h"

namespace ssc::ui
{
using plugin::BandParam;
using plugin::kNumBandParams;

namespace
{
    constexpr const char* kClipboardPrefix = "SamSpectralControl.band.v2:";
    /** Texts copied by 1.0: the parameters before Sidechain, then the speakers; Direction and the
        times as version 1 of the state had them. */
    constexpr const char* kClipboardPrefixV1 = "SamSpectralControl.band.v1:";
    constexpr int kNumBandParamsV1 = static_cast<int> (BandParam::Sidechain);

    template <typename Enum>
    Enum toEnum (float raw, Enum last) noexcept
    {
        return static_cast<Enum> (juce::jlimit (0, static_cast<int> (last), juce::roundToInt (raw)));
    }

    /** One undo step: the band parameters before and after an edit. */
    class SnapshotAction final : public juce::UndoableAction
    {
    public:
        SnapshotAction (juce::AudioProcessorValueTreeState& s, const BandSnapshot& b, const BandSnapshot& a)
            : state (s),
              before (b),
              after (a)
        {
        }

        bool perform() override
        {
            // The edit has already been applied when the action is recorded.
            if (std::exchange (firstPerform, false))
                return true;
            applyBandSnapshot (state, after);
            return true;
        }

        bool undo() override
        {
            applyBandSnapshot (state, before);
            return true;
        }

        int getSizeInUnits() override { return static_cast<int> (sizeof (BandSnapshot) / 64); }

    private:
        juce::AudioProcessorValueTreeState& state;
        BandSnapshot before, after;
        bool firstPerform = true;
    };
} // namespace

filters::BandShape BandValues::toShape() const noexcept
{
    return { shape, frequency, gain, q, slope, character };
}

bool BandSnapshot::operator== (const BandSnapshot& other) const noexcept
{
    for (std::size_t b = 0; b < params.size(); ++b)
        for (std::size_t p = 0; p < params[b].size(); ++p)
            if (! juce::exactlyEqual (params[b][p], other.params[b][p]))
                return false;
    return speakers == other.speakers;
}

BandSnapshot captureBandSnapshot (juce::AudioProcessorValueTreeState& state)
{
    BandSnapshot s {};
    for (int b = 0; b < kNumBands; ++b)
    {
        for (int p = 0; p < kNumBandParams; ++p)
            s.params[static_cast<std::size_t> (b)][static_cast<std::size_t> (p)] =
                state.getParameter (plugin::bandParamId (b, static_cast<BandParam> (p)))->getValue();
        s.speakers[static_cast<std::size_t> (b)] = plugin::readSpeakers (state.state, b);
    }
    return s;
}

void applyBandSnapshot (juce::AudioProcessorValueTreeState& state, const BandSnapshot& snapshot)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        for (int p = 0; p < kNumBandParams; ++p)
        {
            auto* param = state.getParameter (plugin::bandParamId (b, static_cast<BandParam> (p)));
            const float target = snapshot.params[static_cast<std::size_t> (b)][static_cast<std::size_t> (p)];
            if (juce::exactlyEqual (param->getValue(), target))
                continue;
            param->beginChangeGesture();
            param->setValueNotifyingHost (target);
            param->endChangeGesture();
        }
        const auto& speakers = snapshot.speakers[static_cast<std::size_t> (b)];
        if (! (plugin::readSpeakers (state.state, b) == speakers))
            plugin::writeSpeakers (state.state, b, speakers, nullptr);
    }
}

BandModel::BandModel (juce::AudioProcessorValueTreeState& s, juce::UndoManager& um)
    : state (s),
      undoManager (um)
{
    for (int b = 0; b < kNumBands; ++b)
        for (int p = 0; p < kNumBandParams; ++p)
        {
            const auto id = plugin::bandParamId (b, static_cast<BandParam> (p));
            params[static_cast<std::size_t> (b)][static_cast<std::size_t> (p)] = state.getParameter (id);
            raw[static_cast<std::size_t> (b)][static_cast<std::size_t> (p)] = state.getRawParameterValue (id);
            jassert (state.getParameter (id) != nullptr);
        }

    for (int b = 0; b < kNumBands; ++b)
        values[static_cast<std::size_t> (b)] = read (b);
}

juce::RangedAudioParameter& BandModel::parameter (int band, BandParam p) const noexcept
{
    return *params[static_cast<std::size_t> (band)][static_cast<std::size_t> (p)];
}

BandValues BandModel::read (int band) const noexcept
{
    const auto& r = raw[static_cast<std::size_t> (band)];
    auto get = [&r] (BandParam p) { return r[static_cast<std::size_t> (p)]->load (std::memory_order_relaxed); };

    BandValues v;
    v.used = get (BandParam::Used) >= 0.5f;
    v.on = get (BandParam::On) >= 0.5f;
    v.shape = toEnum (get (BandParam::Shape), Shape::AllPass);
    v.frequency = get (BandParam::Frequency);
    v.gain = get (BandParam::Gain);
    v.q = get (BandParam::Q);
    v.slope = toEnum (get (BandParam::Slope), Slope::Brickwall);
    v.mode = toEnum (get (BandParam::Mode), BandMode::Spectral);
    v.target = toEnum (get (BandParam::Target), StereoComponent::Side);
    v.character = toEnum (get (BandParam::Character), Character::Sharp);
    v.autoGain = get (BandParam::AutoGain) >= 0.5f;
    v.sidechain = get (BandParam::Sidechain) >= 0.5f;
    v.upward = get (BandParam::Direction) >= 0.5f;
    v.expand = get (BandParam::Ratio) < 1.0f;
    v.speakers = plugin::readSpeakers (state.state, band);
    return v;
}

bool BandModel::refresh()
{
    bool changed = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto v = read (b);
        if (! (v == values[static_cast<std::size_t> (b)]))
        {
            values[static_cast<std::size_t> (b)] = v;
            changed = true;
        }
    }

    if (! changed)
        return false;

    // Bands deleted elsewhere (undo, preset, automation) leave the selection.
    bool selectionDirty = false;
    for (int b = 0; b < kNumBands; ++b)
        if (selected[static_cast<std::size_t> (b)] && ! values[static_cast<std::size_t> (b)].used)
        {
            selected.reset (static_cast<std::size_t> (b));
            selectionDirty = true;
        }
    if (focused >= 0 && ! values[static_cast<std::size_t> (focused)].used)
    {
        focused = -1;
        for (int b = 0; b < kNumBands && focused < 0; ++b)
            if (selected[static_cast<std::size_t> (b)])
                focused = b;
        selectionDirty = true;
    }
    if (soloBand >= 0 && ! values[static_cast<std::size_t> (soloBand)].active())
        setSoloBand (-1);

    listeners.call ([] (Listener& l) { l.bandsChanged(); });
    if (selectionDirty)
        notifySelectionChanged();
    return true;
}

int BandModel::numUsedBands() const noexcept
{
    int n = 0;
    for (const auto& v : values)
        n += v.used ? 1 : 0;
    return n;
}

void BandModel::notifySelectionChanged()
{
    listeners.call ([] (Listener& l) { l.selectionChanged(); });
}

void BandModel::select (int band, bool addToSelection)
{
    if (! addToSelection)
        selected.reset();
    if (band >= 0 && band < kNumBands)
        selected.set (static_cast<std::size_t> (band));
    focused = band;
    notifySelectionChanged();
}

void BandModel::toggleSelected (int band)
{
    if (band < 0 || band >= kNumBands)
        return;
    selected.flip (static_cast<std::size_t> (band));
    if (selected[static_cast<std::size_t> (band)])
        focused = band;
    else if (focused == band)
        focused = -1;
    notifySelectionChanged();
}

void BandModel::setSelection (const std::bitset<kNumBands>& bands, int focusBand)
{
    if (bands == selected && focusBand == focused)
        return;
    selected = bands;
    focused = focusBand;
    notifySelectionChanged();
}

void BandModel::clearSelection()
{
    if (selected.none() && focused < 0)
        return;
    selected.reset();
    focused = -1;
    notifySelectionChanged();
}

void BandModel::selectAll()
{
    selected.reset();
    for (int b = 0; b < kNumBands; ++b)
        if (values[static_cast<std::size_t> (b)].used)
            selected.set (static_cast<std::size_t> (b));
    if (focused < 0 || ! selected[static_cast<std::size_t> (focused)])
        for (int b = 0; b < kNumBands; ++b)
            if (selected[static_cast<std::size_t> (b)])
            {
                focused = b;
                break;
            }
    notifySelectionChanged();
}

void BandModel::setSoloBand (int band)
{
    if (band == soloBand)
        return;
    soloBand = band;
    if (onSoloChanged)
        onSoloChanged (band);
    listeners.call ([] (Listener& l) { l.bandsChanged(); });
}

void BandModel::beginEdit (const juce::String& transactionName)
{
    if (editDepth++ == 0)
    {
        editName = transactionName;
        editStart = captureBandSnapshot (state);
    }
}

void BandModel::setValue (int band, BandParam p, float plainValue)
{
    jassert (editDepth > 0);
    auto& param = parameter (band, p);
    const float normalised = param.convertTo0to1 (plainValue);

    auto& gestures = inGesture[static_cast<std::size_t> (band)];
    if (! gestures[static_cast<std::size_t> (p)])
    {
        gestures.set (static_cast<std::size_t> (p));
        param.beginChangeGesture();
    }
    if (! juce::exactlyEqual (param.getValue(), normalised))
        param.setValueNotifyingHost (normalised);

    // A Dynamic band's lookahead beyond the global Max Lookahead raises it (DYN-8); the latency
    // follows. Spectral bands do not use it (their STFT frame already sees ahead).
    auto& mode = parameter (band, BandParam::Mode);
    const bool dynamic = static_cast<BandMode> (juce::roundToInt (mode.convertFrom0to1 (mode.getValue()))) == BandMode::Dynamic;
    if (p == BandParam::Lookahead && dynamic)
        if (auto* maxLookahead = state.getParameter (plugin::id::maxLookahead))
            if (maxLookahead->convertFrom0to1 (maxLookahead->getValue()) < plainValue)
            {
                maxLookahead->beginChangeGesture();
                maxLookahead->setValueNotifyingHost (maxLookahead->convertTo0to1 (plainValue));
                maxLookahead->endChangeGesture();
            }
}

void BandModel::endEdit()
{
    jassert (editDepth > 0);
    if (--editDepth > 0)
        return;

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& gestures = inGesture[static_cast<std::size_t> (b)];
        for (int p = 0; p < kNumBandParams; ++p)
            if (gestures[static_cast<std::size_t> (p)])
                parameter (b, static_cast<BandParam> (p)).endChangeGesture();
        gestures.reset();
    }

    const auto after = captureBandSnapshot (state);
    if (! (after == editStart))
    {
        undoManager.beginNewTransaction (editName);
        undoManager.perform (new SnapshotAction (state, editStart, after), editName);
    }
    refresh();
}

void BandModel::setToDefault (int band, BandParam p)
{
    auto& param = parameter (band, p);
    setValue (band, p, param.convertFrom0to1 (param.getDefaultValue()));
}

int BandModel::createBand (Shape shape, float frequency, float gain)
{
    int band = -1;
    for (int b = 0; b < kNumBands && band < 0; ++b)
        if (! values[static_cast<std::size_t> (b)].used)
            band = b;
    if (band < 0)
        return -1;

    {
        ScopedEdit edit (*this, "Create Band");
        for (int p = 0; p < kNumBandParams; ++p)
            setToDefault (band, static_cast<BandParam> (p));
        setSpeakers (band, {});
        setValue (band, BandParam::Used, 1.0f);
        setValue (band, BandParam::On, 1.0f);
        setValue (band, BandParam::Shape, static_cast<float> (shape));
        setValue (band, BandParam::Frequency, frequency);
        setValue (band, BandParam::Gain, shapeUsesGain (shape) ? gain : 0.0f);
        setValue (band, BandParam::Q, defaultQFor (shape));
        setValue (band, BandParam::Slope, static_cast<float> (defaultSlopeFor (shape)));
    }
    refresh();
    select (band, false);
    return band;
}

void BandModel::deleteBand (int band)
{
    if (band < 0 || band >= kNumBands || ! values[static_cast<std::size_t> (band)].used)
        return;

    if (soloBand == band)
        setSoloBand (-1);
    ScopedEdit edit (*this, "Delete Band");
    for (int p = 0; p < kNumBandParams; ++p)
        setToDefault (band, static_cast<BandParam> (p));
    setSpeakers (band, {});
}

void BandModel::deleteSelected()
{
    if (selected.none())
        return;
    ScopedEdit edit (*this, "Delete Bands");
    for (int b = 0; b < kNumBands; ++b)
        if (selected[static_cast<std::size_t> (b)])
            deleteBand (b);
}

void BandModel::resetBand (int band)
{
    ScopedEdit edit (*this, "Reset Band");
    for (int p = 0; p < kNumBandParams; ++p)
    {
        const auto param = static_cast<BandParam> (p);
        if (param == BandParam::Used || param == BandParam::Shape || param == BandParam::Frequency)
            continue;
        setToDefault (band, param);
    }
    const auto shape = values[static_cast<std::size_t> (band)].shape;
    setValue (band, BandParam::Q, defaultQFor (shape));
    setValue (band, BandParam::Slope, static_cast<float> (defaultSlopeFor (shape)));
    setSpeakers (band, {});
}

void BandModel::setShape (int band, Shape shape)
{
    const auto& v = values[static_cast<std::size_t> (band)];
    ScopedEdit edit (*this, "Change Shape");
    setValue (band, BandParam::Shape, static_cast<float> (shape));
    setValue (band, BandParam::Slope, static_cast<float> (nearestAllowedSlope (shape, v.slope)));
    if (! shapeUsesGain (shape) && v.mode != BandMode::Static)
        setValue (band, BandParam::Mode, static_cast<float> (BandMode::Static));
}

void BandModel::setMode (int band, BandMode mode)
{
    struct Defaults
    {
        ThresholdMode mode;
        float threshold;
    };
    constexpr Defaults dynamicDefaults { ThresholdMode::Absolute, -20.0f };
    constexpr Defaults spectralDefaults { ThresholdMode::Relative, 6.0f };

    const auto& v = values[static_cast<std::size_t> (band)];
    if (mode != BandMode::Static && ! shapeUsesGain (v.shape))
        return;

    ScopedEdit edit (*this, "Change Mode");
    setValue (band, BandParam::Mode, static_cast<float> (mode));

    if (mode == BandMode::Static || v.mode == mode)
        return;
    const auto& from = mode == BandMode::Spectral ? dynamicDefaults : spectralDefaults;
    const auto& to = mode == BandMode::Spectral ? spectralDefaults : dynamicDefaults;
    auto& thresholdMode = parameter (band, BandParam::ThresholdMode);
    auto& threshold = parameter (band, BandParam::Threshold);
    const auto currentMode = static_cast<ThresholdMode> (juce::roundToInt (thresholdMode.convertFrom0to1 (thresholdMode.getValue())));
    const float currentThreshold = threshold.convertFrom0to1 (threshold.getValue());
    if (currentMode == from.mode && std::abs (currentThreshold - from.threshold) < 0.05f)
    {
        setValue (band, BandParam::ThresholdMode, static_cast<float> (to.mode));
        setValue (band, BandParam::Threshold, to.threshold);
    }
}

engine::SpeakerSelection BandModel::speakers (int band) const
{
    return plugin::readSpeakers (state.state, band);
}

void BandModel::setSpeakers (int band, const engine::SpeakerSelection& selection)
{
    jassert (editDepth > 0); // recorded by the edit's snapshot
    if (! (speakers (band) == selection))
        plugin::writeSpeakers (state.state, band, selection, nullptr);
}

void BandModel::copyBand (int band) const
{
    juce::SystemClipboard::copyTextToClipboard (bandText (band));
}

juce::String BandModel::bandText (int band) const
{
    juce::StringArray items;
    for (int p = 0; p < kNumBandParams; ++p)
        items.add (juce::String (parameter (band, static_cast<BandParam> (p)).getValue(), 9));
    items.add (plugin::speakerSelectionToString (speakers (band)));
    return kClipboardPrefix + items.joinIntoString (",");
}

bool BandModel::canPaste() const
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    return text.startsWith (kClipboardPrefix) || text.startsWith (kClipboardPrefixV1);
}

bool BandModel::pasteToBand (int band)
{
    return pasteBandText (band, juce::SystemClipboard::getTextFromClipboard());
}

bool BandModel::pasteBandText (int band, const juce::String& text)
{
    const bool v1 = text.startsWith (kClipboardPrefixV1);
    if (! v1 && ! text.startsWith (kClipboardPrefix))
        return false;

    juce::StringArray items;
    items.addTokens (text.fromFirstOccurrenceOf (":", false, false), ",", {});
    const int numParams = v1 ? kNumBandParamsV1 : kNumBandParams;
    if (items.size() != numParams && items.size() != numParams + 1)
        return false;

    // Plain values; parameters newer than the text take their default.
    std::array<float, kNumBandParams> plain {};
    for (int p = 0; p < kNumBandParams; ++p)
    {
        auto& target = parameter (band, static_cast<BandParam> (p));
        const float normalised = p < numParams ? juce::jlimit (0.0f, 1.0f, items[p].getFloatValue()) : target.getDefaultValue();
        plain[static_cast<std::size_t> (p)] = target.convertFrom0to1 (normalised);
    }
    if (v1)
    {
        // Copied by 1.0: its direction and times as this version reads them (same sound).
        auto at = [&plain] (BandParam p) -> float& { return plain[static_cast<std::size_t> (p)]; };
        const auto now = plugin::migrateBandDirection (
            { at (BandParam::Direction), at (BandParam::Ratio), at (BandParam::Attack), at (BandParam::Release) });
        at (BandParam::Direction) = now.direction;
        at (BandParam::Attack) = now.attackMs;
        at (BandParam::Release) = now.releaseMs;
    }

    ScopedEdit edit (*this, "Paste Band");
    for (int p = 0; p < kNumBandParams; ++p)
        if (static_cast<BandParam> (p) != BandParam::Used)
            setValue (band, static_cast<BandParam> (p), plain[static_cast<std::size_t> (p)]);
    // The speaker selection follows (texts copied before it existed give the default).
    setSpeakers (band, items.size() > numParams ? plugin::speakerSelectionFromString (items[numParams]) : engine::SpeakerSelection {});
    return true;
}
} // namespace ssc::ui
