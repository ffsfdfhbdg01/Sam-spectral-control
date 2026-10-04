#pragma once

#include <array>
#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/AnalyzerService.h"
#include "plugin/AnalyzerSettings.h"
#include "ssc/spectral/SpectralEngine.h"
#include "ui/BandLogic.h"
#include "ui/BandModel.h"
#include "ui/ResponseCurves.h"

namespace ssc::ui
{
/**
    Central display: frequency/level grid, pre/post analyzer, band curves and nodes.

    Interaction (research §5c, BAND-4…BAND-6):
    - double click on the background creates a band whose shape depends on the position
      (shapeForGesture); a hover preview shows what would be created
    - pressing on the composite curve and dragging creates a bell, or a shelf near the ends
    - drag a node: frequency and gain (Q for shapes without gain); Shift = fine; wheel = Q
    - Alt/Option + press on a node solos it while held; double click on a node toggles it
    - drag on the background draws a selection rectangle; selected bands move together
    - right click on a node opens the band menu
    - keys: Delete/Backspace deletes the selection, Cmd/Ctrl+Z undo, Cmd/Ctrl+Shift+Z or
      Cmd/Ctrl+Y redo, Cmd/Ctrl+A selects all bands, Escape clears the selection
    Analyzer (research §11c): pre/post spectra, peak hold, and a glow over the most active regions
    of the main spectrum (VIS-2); Dynamic and Spectral bands glow with their current gain change
    (VIS-3).
*/
class EqDisplay final : public juce::Component, private BandModel::Listener, private juce::Timer
{
public:
    EqDisplay (plugin::AnalyzerService& analyzer, BandModel& model);
    ~EqDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Polls the band parameters and the newest analyzer frame; called on every vblank. */
    void refreshFromAnalyzer();

    void setSampleRate (double sampleRate);
    /** See ResponseCurves::setResponseModel(). */
    void setResponseModel (const ResponseCurves::Model& responseModel);

    /** Source of the live dynamic gain change of each band (DYN-11); polled on every frame. */
    std::function<float (int band)> dynamicGainProvider;

    /** Source of the live gain change of Spectral bands over frequency; polled on every frame. */
    std::function<const spectral::SpectralEngine::MaskView*()> spectralViewProvider;
    void setChannelInfo (int numChannels, int numPairs);

    /** Analyzer display settings (mode, range, freeze, peak hold, glow). */
    void setAnalyzerSettings (const plugin::AnalyzerSettings& settings);

    /** Visible EQ range: ±range dB (3, 6, 12, 18 or 30). */
    void setEqRange (float rangeDb);
    float getEqRange() const noexcept { return eqRangeDb; }

    static constexpr float kMinHz = 10.0f;
    static constexpr float kMaxHz = 30000.0f;
    static constexpr float kAnalyzerTopDb = 6.0f;
    static constexpr float kNodeRadius = 7.0f;

    float xForFrequency (float hz) const noexcept;
    float frequencyForX (float x) const noexcept;
    float yForEqDb (float db) const noexcept;
    float eqDbForY (float y) const noexcept;
    float yForAnalyzerDb (float db) const noexcept;

    /** Node centre of a band in component coordinates. */
    juce::Point<float> nodePosition (int band) const;

private:
    enum class DragMode
    {
        None,
        PendingCurve, ///< pressed on the composite curve; becomes a new band once dragged
        Bands,
        Lasso
    };

    struct Ghost
    {
        filters::BandShape shape;
        std::vector<float> curve;
    };

    // BandModel::Listener
    void bandsChanged() override;
    void selectionChanged() override;

    // juce::Timer: ends a group of wheel edits
    void timerCallback() override;

    void syncCurves();
    int hitTestNode (juce::Point<float>) const;
    bool isOnCompositeCurve (juce::Point<float>) const;
    float compositeDbAtX (float x) const;
    juce::Point<float> normalised (juce::Point<float>) const noexcept;
    void updateHover (juce::Point<float>);
    void startBandDrag (int band, juce::Point<float> position);
    void dragBands (const juce::MouseEvent&);
    void createFromCurve (juce::Point<float> position);
    void showBandMenu (int band);
    std::vector<int> menuTargets (int band) const;

    void paintGrid (juce::Graphics&) const;
    void paintSpectrum (juce::Graphics&, const plugin::SpectrumFrame&, bool post, bool main) const;
    void paintPeakHold (juce::Graphics&, const plugin::SpectrumFrame&) const;
    void paintGlow (juce::Graphics&, const plugin::SpectrumFrame&);
    void paintBandCurves (juce::Graphics&) const;
    void paintComposite (juce::Graphics&) const;
    void paintGhost (juce::Graphics&) const;
    void paintNodes (juce::Graphics&) const;
    void paintHoverLabel (juce::Graphics&) const;
    void paintHint (juce::Graphics&) const;
    juce::Path curvePath (const std::vector<float>& db) const;

    plugin::AnalyzerService& analyzer;
    BandModel& model;
    ResponseCurves curves;
    bool hasFrame = false;
    juce::VBlankAttachment vblank;

    plugin::AnalyzerSettings analyzerSettings;
    juce::Image background; ///< gradient and grid (see paint())
    bool backgroundDirty = true;
    std::vector<float> glowActivity, glowScratch, glowLineY; ///< scratch of paintGlow()

    float eqRangeDb = 18.0f;
    int numChannels = 2;
    int numPairs = 1;

    int hoveredBand = -1;
    bool hoverOnCurve = false;
    std::optional<Ghost> ghost;

    DragMode dragMode = DragMode::None;
    juce::Point<float> dragStart;
    int dragBand = -1;
    bool dragFine = false;
    bool soloHeld = false;
    bool dragEditOpen = false;
    std::array<BandValues, kNumBands> dragStartValues {};
    std::bitset<kNumBands> lassoBase;
    juce::Rectangle<float> lasso;

    bool wheelEditOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};
} // namespace ssc::ui
