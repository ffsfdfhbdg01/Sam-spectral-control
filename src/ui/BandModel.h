#pragma once

#include <array>
#include <bitset>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/Parameters.h"
#include "ssc/engine/ChannelTopology.h"
#include "ssc/filters/BandDesign.h"

namespace ssc::ui
{
/** Plain values of one band as shown and edited in the UI. */
struct BandValues
{
    bool used = false;
    bool on = true;
    Shape shape = Shape::Bell;
    float frequency = 1000.0f;
    float gain = 0.0f;
    float q = 1.0f;
    Slope slope = Slope::Db12;
    BandMode mode = BandMode::Static;
    StereoComponent target = StereoComponent::All;
    Character character = Character::Clear;
    bool autoGain = false;
    engine::SpeakerSelection speakers; ///< loudspeakers of the band (state, not a parameter; CH-4)

    bool active() const noexcept { return used && on; }
    filters::BandShape toShape() const noexcept;
    /** Exact comparison, used to detect parameter changes. */
    bool operator== (const BandValues& o) const noexcept
    {
        constexpr std::equal_to<float> same;
        return used == o.used && on == o.on && shape == o.shape && same (frequency, o.frequency) && same (gain, o.gain) && same (q, o.q)
            && slope == o.slope && mode == o.mode && target == o.target && character == o.character && autoGain == o.autoGain
            && speakers == o.speakers;
    }
};

/** Normalised values of every band parameter and the speaker selections: the unit of undo. */
struct BandSnapshot
{
    std::array<std::array<float, plugin::kNumBandParams>, kNumBands> params {};
    std::array<engine::SpeakerSelection, kNumBands> speakers {};

    bool operator== (const BandSnapshot& other) const noexcept;
};

BandSnapshot captureBandSnapshot (juce::AudioProcessorValueTreeState& state);

/** Sets every band parameter that differs from the snapshot, each as a complete host gesture. */
void applyBandSnapshot (juce::AudioProcessorValueTreeState& state, const BandSnapshot& snapshot);

/**
    Message-thread model of the 24 bands on top of the parameter tree.

    - Values are polled from the parameters (refresh()), so host automation, presets and the UI
      all go through the same path.
    - Edits are transactions: the first change of a parameter starts its host gesture, endEdit()
      ends all gestures and records one undo step (a mouse gesture or a menu action = one step).
    - Undo stores snapshots of the band parameters; the UndoManager belongs to the processor,
      so the history survives closing the editor.
    - Selection and solo are UI state.
*/
class BandModel
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void bandsChanged() {}
        virtual void selectionChanged() {}
    };

    BandModel (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager);

    /** Re-reads all band parameters; notifies listeners and returns true if anything changed. */
    bool refresh();

    const BandValues& band (int index) const noexcept { return values[static_cast<std::size_t> (index)]; }
    juce::RangedAudioParameter& parameter (int band, plugin::BandParam p) const noexcept;
    int numUsedBands() const noexcept;

    // Selection ------------------------------------------------------------------------------
    int getFocusedBand() const noexcept { return focused; }
    bool isSelected (int band) const noexcept { return band >= 0 && band < kNumBands && selected[static_cast<std::size_t> (band)]; }
    const std::bitset<kNumBands>& getSelection() const noexcept { return selected; }
    void select (int band, bool addToSelection);
    void toggleSelected (int band);
    void setSelection (const std::bitset<kNumBands>& bands, int focusBand);
    void clearSelection();
    void selectAll();

    // Solo (monitoring only) ---------------------------------------------------------------
    std::function<void (int)> onSoloChanged;
    int getSoloBand() const noexcept { return soloBand; }
    void setSoloBand (int band);

    // Editing ------------------------------------------------------------------------------
    void beginEdit (const juce::String& transactionName);
    void setValue (int band, plugin::BandParam p, float plainValue);
    void endEdit();
    bool isEditing() const noexcept { return editDepth > 0; }

    struct ScopedEdit
    {
        ScopedEdit (BandModel& m, const juce::String& name)
            : model (m)
        {
            model.beginEdit (name);
        }
        ~ScopedEdit() { model.endEdit(); }
        BandModel& model;
        JUCE_DECLARE_NON_COPYABLE (ScopedEdit)
    };

    /** Creates a band in the first free slot, selects it and returns its index (−1 if all are used). */
    int createBand (Shape shape, float frequency, float gain);
    void deleteBand (int band);
    void deleteSelected();
    /** Restores the defaults of everything except shape and frequency. */
    void resetBand (int band);
    /** Changes the shape and keeps the band consistent (slope, mode). */
    void setShape (int band, Shape shape);

    /**
        Changes the band mode. Thresholds still at the defaults of the previous mode move to the
        defaults of the new one (Dynamic: Absolute −20 dBFS, Spectral: Relative +6 dB, DYN-4).
    */
    void setMode (int band, BandMode mode);

    /** Loudspeaker selection of a band (CH-4); setSpeakers() must be called inside an edit. */
    engine::SpeakerSelection speakers (int band) const;
    void setSpeakers (int band, const engine::SpeakerSelection& selection);

    void copyBand (int band) const;
    bool canPaste() const;
    bool pasteToBand (int band);

    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    BandValues read (int band) const noexcept;
    void setToDefault (int band, plugin::BandParam p);
    void notifySelectionChanged();

    juce::AudioProcessorValueTreeState& state;
    juce::UndoManager& undoManager;
    std::array<std::array<juce::RangedAudioParameter*, plugin::kNumBandParams>, kNumBands> params {};
    std::array<std::array<std::atomic<float>*, plugin::kNumBandParams>, kNumBands> raw {};
    std::array<BandValues, kNumBands> values {};

    std::bitset<kNumBands> selected;
    int focused = -1;
    int soloBand = -1;

    int editDepth = 0;
    juce::String editName;
    BandSnapshot editStart {};
    std::array<std::bitset<plugin::kNumBandParams>, kNumBands> inGesture {};

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandModel)
};
} // namespace ssc::ui
