#include "ui/BandModel.h"

#include <utility>

#include "ui/BandLogic.h"

namespace ssc::ui
{
using plugin::BandParam;
using plugin::kNumBandParams;

namespace
{
    constexpr const char* kClipboardPrefix = "SamSpectralControl.band.v1:";

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
    return { shape, frequency, gain, q, slope };
}

BandSnapshot captureBandSnapshot (juce::AudioProcessorValueTreeState& state)
{
    BandSnapshot s {};
    for (int b = 0; b < kNumBands; ++b)
        for (int p = 0; p < kNumBandParams; ++p)
            s[static_cast<std::size_t> (b)][static_cast<std::size_t> (p)] =
                state.getParameter (plugin::bandParamId (b, static_cast<BandParam> (p)))->getValue();
    return s;
}

void applyBandSnapshot (juce::AudioProcessorValueTreeState& state, const BandSnapshot& snapshot)
{
    for (int b = 0; b < kNumBands; ++b)
        for (int p = 0; p < kNumBandParams; ++p)
        {
            auto* param = state.getParameter (plugin::bandParamId (b, static_cast<BandParam> (p)));
            const float target = snapshot[static_cast<std::size_t> (b)][static_cast<std::size_t> (p)];
            if (juce::exactlyEqual (param->getValue(), target))
                continue;
            param->beginChangeGesture();
            param->setValueNotifyingHost (target);
            param->endChangeGesture();
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

    // A band lookahead beyond the global Max Lookahead raises it (DYN-8); the latency follows.
    if (p == BandParam::Lookahead)
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
    if (after != editStart)
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

void BandModel::copyBand (int band) const
{
    juce::StringArray items;
    for (int p = 0; p < kNumBandParams; ++p)
        items.add (juce::String (parameter (band, static_cast<BandParam> (p)).getValue(), 9));
    juce::SystemClipboard::copyTextToClipboard (kClipboardPrefix + items.joinIntoString (","));
}

bool BandModel::canPaste() const
{
    return juce::SystemClipboard::getTextFromClipboard().startsWith (kClipboardPrefix);
}

bool BandModel::pasteToBand (int band)
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    if (! text.startsWith (kClipboardPrefix))
        return false;

    juce::StringArray items;
    items.addTokens (text.fromFirstOccurrenceOf (":", false, false), ",", {});
    if (items.size() != kNumBandParams)
        return false;

    ScopedEdit edit (*this, "Paste Band");
    for (int p = 0; p < kNumBandParams; ++p)
    {
        const auto param = static_cast<BandParam> (p);
        if (param == BandParam::Used)
            continue;
        auto& target = parameter (band, param);
        setValue (band, param, target.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, items[p].getFloatValue())));
    }
    return true;
}
} // namespace ssc::ui
