#include "ui/EqDisplay.h"

#include <cmath>

#include "plugin/Parameters.h"
#include "ui/Theme.h"

namespace ssc::ui
{
using plugin::BandParam;

namespace
{
    constexpr float kCurveHitPixels = 6.0f;
    constexpr float kNodeHitPixels = EqDisplay::kNodeRadius + 4.0f;
    constexpr float kDragThreshold = 3.0f;
    constexpr float kFineFactor = 0.1f;
    constexpr float kMinFrequency = 10.0f;
    constexpr float kMaxFrequency = 30000.0f;
    constexpr float kMaxGain = 30.0f;
    constexpr float kMinQ = 0.025f;
    constexpr float kMaxQ = 40.0f;
    constexpr int kWheelGroupMs = 400;

    juce::String qText (float q)
    {
        return juce::String (q, q < 10.0f ? 2 : 1);
    }

    int gridStepFor (float rangeDb) noexcept
    {
        if (rangeDb <= 3.0f)
            return 1;
        if (rangeDb <= 6.0f)
            return 2;
        if (rangeDb <= 12.0f)
            return 3;
        if (rangeDb <= 18.0f)
            return 6;
        return 10;
    }

    /** Level at which the node of a shape is drawn: its gain, or the response at fc for cuts. */
    float nodeDbFor (const filters::BandShape& shape, const ResponseCurves& curves)
    {
        if (shapeUsesGain (shape.shape))
            return static_cast<float> (shape.gainDb);
        if (shape.shape == Shape::LowCut || shape.shape == Shape::HighCut)
            return curves.responseAt (shape, static_cast<float> (shape.frequencyHz));
        return 0.0f;
    }
} // namespace

EqDisplay::EqDisplay (plugin::AnalyzerService& analyzerToUse, BandModel& modelToUse)
    : analyzer (analyzerToUse),
      model (modelToUse),
      vblank (this, [this] { refreshFromAnalyzer(); })
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    model.addListener (this);
}

EqDisplay::~EqDisplay()
{
    if (wheelEditOpen)
        model.endEdit();
    model.removeListener (this);
}

void EqDisplay::refreshFromAnalyzer()
{
    model.refresh();

    if (dynamicGainProvider)
    {
        bool changed = false;
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& v = model.band (b);
            if (v.mode == BandMode::Spectral)
                continue; // drawn from the spectral view below
            const float gain = v.active() && v.mode == BandMode::Dynamic ? dynamicGainProvider (b) : 0.0f;
            changed = curves.setDynamicGain (b, gain) || changed;
        }
        if (changed)
        {
            curves.updateComposite();
            repaint();
        }
    }

    if (spectralViewProvider)
        if (const auto* view = spectralViewProvider())
        {
            for (int b = 0; b < kNumBands; ++b)
            {
                const auto& v = model.band (b);
                if (v.mode != BandMode::Spectral)
                    continue;
                const bool live = v.active() && view->bandActive[static_cast<std::size_t> (b)];
                curves.setSpectralGains (b, view->hz.data(), view->bandDb[static_cast<std::size_t> (b)].data(), live ? view->numPoints : 0);
            }
            curves.updateComposite();
            repaint();
        }

    if (analyzer.frames().update())
    {
        hasFrame = true;
        repaint();
    }
}

void EqDisplay::resized()
{
    curves.setGrid (juce::jlimit (128, 1024, getWidth() / 2), kMinHz, kMaxHz);
    syncCurves();
}

void EqDisplay::setSampleRate (double sampleRate)
{
    if (sampleRate <= 0.0 || juce::exactlyEqual (sampleRate, curves.getSampleRate()))
        return;
    curves.setSampleRate (sampleRate);
    repaint();
}

void EqDisplay::setChannelInfo (int channels, int pairs)
{
    numChannels = channels;
    numPairs = pairs;
}

void EqDisplay::setEqRange (float rangeDb)
{
    eqRangeDb = juce::jlimit (1.0f, 60.0f, rangeDb);
    repaint();
}

void EqDisplay::bandsChanged()
{
    syncCurves();
    if (hoveredBand >= 0 && ! model.band (hoveredBand).used)
        hoveredBand = -1;
    repaint();
}

void EqDisplay::selectionChanged()
{
    repaint();
}

void EqDisplay::syncCurves()
{
    bool changed = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& v = model.band (b);
        const std::optional<filters::BandShape> shape = v.active() ? std::optional<filters::BandShape> (v.toShape()) : std::nullopt;
        changed = curves.updateBand (b, shape) || changed;
    }
    if (changed)
        curves.updateComposite();
}

//==============================================================================
float EqDisplay::xForFrequency (float hz) const noexcept
{
    return static_cast<float> (getWidth()) * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
}

float EqDisplay::frequencyForX (float x) const noexcept
{
    return kMinHz * std::pow (kMaxHz / kMinHz, x / static_cast<float> (juce::jmax (1, getWidth())));
}

float EqDisplay::yForEqDb (float db) const noexcept
{
    const auto h = static_cast<float> (getHeight());
    return h * 0.5f - (db / eqRangeDb) * h * 0.45f;
}

float EqDisplay::eqDbForY (float y) const noexcept
{
    const auto h = static_cast<float> (juce::jmax (1, getHeight()));
    return (h * 0.5f - y) / (h * 0.45f) * eqRangeDb;
}

float EqDisplay::yForAnalyzerDb (float db) const noexcept
{
    const auto h = static_cast<float> (getHeight());
    return juce::jmap (db, kAnalyzerTopDb, kAnalyzerBottomDb, 0.0f, h);
}

juce::Point<float> EqDisplay::nodePosition (int band) const
{
    const auto& v = model.band (band);
    const auto h = static_cast<float> (getHeight());
    const float y = yForEqDb (nodeDbFor (v.toShape(), curves));
    return { xForFrequency (v.frequency), juce::jlimit (kNodeRadius + 1.0f, h - kNodeRadius - 1.0f, y) };
}

juce::Point<float> EqDisplay::normalised (juce::Point<float> p) const noexcept
{
    return { p.x / static_cast<float> (juce::jmax (1, getWidth())), p.y / static_cast<float> (juce::jmax (1, getHeight())) };
}

int EqDisplay::hitTestNode (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = kNodeHitPixels;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! model.band (b).used)
            continue;
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d <= bestDistance)
        {
            best = b;
            bestDistance = d;
        }
    }
    return best;
}

float EqDisplay::compositeDbAtX (float x) const
{
    const auto& db = curves.compositeDb();
    const int n = curves.numPoints();
    if (n < 2 || static_cast<int> (db.size()) != n)
        return 0.0f;

    const float position = juce::jlimit (0.0f, static_cast<float> (n - 1),
                                         std::log (frequencyForX (x) / kMinHz) / std::log (kMaxHz / kMinHz) * static_cast<float> (n - 1));
    const int i = juce::jmin (n - 2, static_cast<int> (position));
    const float t = position - static_cast<float> (i);
    return db[static_cast<std::size_t> (i)] * (1.0f - t) + db[static_cast<std::size_t> (i + 1)] * t;
}

bool EqDisplay::isOnCompositeCurve (juce::Point<float> p) const
{
    if (frequencyForX (p.x) >= 0.5f * static_cast<float> (curves.getSampleRate()))
        return false;
    return std::abs (yForEqDb (compositeDbAtX (p.x)) - p.y) <= kCurveHitPixels;
}

//==============================================================================
void EqDisplay::updateHover (juce::Point<float> p)
{
    const int node = hitTestNode (p);
    const bool onCurve = node < 0 && isOnCompositeCurve (p);

    std::optional<Ghost> newGhost;
    if (node < 0 && model.numUsedBands() < kNumBands)
    {
        const auto n = normalised (p);
        filters::BandShape s;
        s.frequencyHz = frequencyForX (p.x);
        if (onCurve)
        {
            s.shape = shapeForGesture (n.x, n.y, CreateGesture::DragCurve);
            s.gainDb = 6.0;
        }
        else
        {
            s.shape = shapeForGesture (n.x, n.y, CreateGesture::DoubleClick);
            s.gainDb = shapeUsesGain (s.shape) ? juce::jlimit (-kMaxGain, kMaxGain, eqDbForY (p.y)) : 0.0f;
        }
        s.q = defaultQFor (s.shape);
        s.slope = defaultSlopeFor (s.shape);
        newGhost = Ghost { s, curves.responseOf (s) };
    }

    hoveredBand = node;
    hoverOnCurve = onCurve;
    ghost = std::move (newGhost);
    setMouseCursor (node >= 0 ? juce::MouseCursor::DraggingHandCursor
                              : (onCurve ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
    repaint();
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    updateHover (e.position);
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    hoveredBand = -1;
    hoverOnCurve = false;
    ghost.reset();
    repaint();
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    ghost.reset();
    const auto p = e.position;
    const int band = hitTestNode (p);

    if (e.mods.isPopupMenu())
    {
        if (band >= 0)
        {
            if (! model.isSelected (band))
                model.select (band, false);
            showBandMenu (band);
        }
        return;
    }

    if (band >= 0)
    {
        if (e.mods.isCommandDown())
            model.toggleSelected (band);
        else if (! model.isSelected (band))
            model.select (band, false);
        else
            model.setSelection (model.getSelection(), band);

        if (e.mods.isAltDown() && model.band (band).on)
        {
            soloHeld = true;
            model.setSoloBand (band);
        }
        model.beginEdit ("Move Band");
        dragEditOpen = true;
        startBandDrag (band, p);
        return;
    }

    if (isOnCompositeCurve (p) && model.numUsedBands() < kNumBands)
    {
        dragMode = DragMode::PendingCurve;
        dragStart = p;
        return;
    }

    dragMode = DragMode::Lasso;
    dragStart = p;
    lasso = {};
    lassoBase = e.mods.isCommandDown() ? model.getSelection() : std::bitset<kNumBands> {};
    if (! e.mods.isCommandDown())
        model.clearSelection();
}

void EqDisplay::startBandDrag (int band, juce::Point<float> position)
{
    dragMode = DragMode::Bands;
    dragBand = band;
    dragStart = position;
    dragFine = false;
    for (int b = 0; b < kNumBands; ++b)
        dragStartValues[static_cast<std::size_t> (b)] = model.band (b);
}

void EqDisplay::createFromCurve (juce::Point<float> position)
{
    const auto n = normalised (position);
    const auto shape = shapeForGesture (n.x, n.y, CreateGesture::DragCurve);

    // Creation and the drag that follows are one undo step.
    model.beginEdit ("Create Band");
    const int band = model.createBand (shape, frequencyForX (position.x), 0.0f);
    if (band < 0)
    {
        model.endEdit();
        dragMode = DragMode::None;
        return;
    }
    dragEditOpen = true;
    startBandDrag (band, position);
}

void EqDisplay::dragBands (const juce::MouseEvent& e)
{
    const bool fine = e.mods.isShiftDown();
    if (fine != dragFine)
    {
        // Re-anchor so that switching precision mid-drag does not make the band jump.
        dragFine = fine;
        dragStart = e.position;
        for (int b = 0; b < kNumBands; ++b)
            dragStartValues[static_cast<std::size_t> (b)] = model.band (b);
    }

    const float scale = fine ? kFineFactor : 1.0f;
    const float dx = (e.position.x - dragStart.x) * scale;
    const float dy = (e.position.y - dragStart.y) * scale;
    const float ratio = std::pow (kMaxHz / kMinHz, dx / static_cast<float> (juce::jmax (1, getWidth())));
    const float dbPerPixel = eqRangeDb / (0.45f * static_cast<float> (juce::jmax (1, getHeight())));

    auto apply = [&] (int b) {
        const auto& start = dragStartValues[static_cast<std::size_t> (b)];
        if (! start.used)
            return;
        model.setValue (b, BandParam::Frequency, juce::jlimit (kMinFrequency, kMaxFrequency, start.frequency * ratio));
        if (shapeUsesGain (start.shape))
            model.setValue (b, BandParam::Gain, juce::jlimit (-kMaxGain, kMaxGain, start.gain - dy * dbPerPixel));
        else if (shapeUsesQ (start.shape))
            model.setValue (b, BandParam::Q, juce::jlimit (kMinQ, kMaxQ, start.q * std::exp (-dy * 0.015f)));
    };

    if (model.isSelected (dragBand))
    {
        for (int b = 0; b < kNumBands; ++b)
            if (model.isSelected (b))
                apply (b);
    }
    else
    {
        apply (dragBand);
    }
    model.refresh();
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    switch (dragMode)
    {
        case DragMode::PendingCurve:
            if (e.position.getDistanceFrom (dragStart) >= kDragThreshold)
            {
                createFromCurve (dragStart);
                if (dragMode == DragMode::Bands)
                    dragBands (e);
            }
            break;

        case DragMode::Bands: dragBands (e); break;

        case DragMode::Lasso:
        {
            lasso = juce::Rectangle<float> (dragStart, e.position);
            auto selection = lassoBase;
            int focus = -1;
            for (int b = 0; b < kNumBands; ++b)
                if (model.band (b).used && lasso.contains (nodePosition (b)))
                {
                    selection.set (static_cast<std::size_t> (b));
                    if (focus < 0)
                        focus = b;
                }
            if (focus < 0 && selection.any())
                focus = model.getFocusedBand();
            model.setSelection (selection, focus);
            repaint();
            break;
        }

        case DragMode::None: break;
    }
}

void EqDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (dragEditOpen)
    {
        dragEditOpen = false;
        model.endEdit();
    }
    if (soloHeld)
    {
        soloHeld = false;
        model.setSoloBand (-1);
    }
    if (dragMode == DragMode::Lasso)
        lasso = {};

    dragMode = DragMode::None;
    dragBand = -1;
    updateHover (e.position);
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = hitTestNode (e.position);
    if (band >= 0)
    {
        BandModel::ScopedEdit edit (model, "Toggle Band");
        model.setValue (band, BandParam::On, model.band (band).on ? 0.0f : 1.0f);
        return;
    }

    const auto n = normalised (e.position);
    const auto shape = shapeForGesture (n.x, n.y, CreateGesture::DoubleClick);
    const float gain =
        shapeUsesGain (shape) ? std::round (juce::jlimit (-kMaxGain, kMaxGain, eqDbForY (e.position.y)) * 10.0f) / 10.0f : 0.0f;
    model.createBand (shape, frequencyForX (e.position.x), gain);
    updateHover (e.position);
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int band = hitTestNode (e.position);
    if (band < 0 || ! shapeUsesQ (model.band (band).shape))
        return;

    if (! wheelEditOpen)
    {
        model.beginEdit ("Change Q");
        wheelEditOpen = true;
    }
    startTimer (kWheelGroupMs);

    const float octaves = wheel.deltaY * (e.mods.isShiftDown() ? 0.2f : 2.0f);
    auto apply = [&] (int b) {
        const auto& v = model.band (b);
        if (v.used && shapeUsesQ (v.shape))
            model.setValue (b, BandParam::Q, juce::jlimit (kMinQ, kMaxQ, v.q * std::pow (2.0f, octaves)));
    };
    if (model.isSelected (band))
    {
        for (int b = 0; b < kNumBands; ++b)
            if (model.isSelected (b))
                apply (b);
    }
    else
    {
        apply (band);
    }
    model.refresh();
}

bool EqDisplay::keyPressed (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        model.deleteSelected();
        return true;
    }
    if (key == juce::KeyPress ('z', command, 0))
    {
        model.getUndoManager().undo();
        model.refresh();
        return true;
    }
    if (key == juce::KeyPress ('z', command | shift, 0) || key == juce::KeyPress ('y', command, 0))
    {
        model.getUndoManager().redo();
        model.refresh();
        return true;
    }
    if (key == juce::KeyPress ('a', command, 0))
    {
        model.selectAll();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        model.clearSelection();
        return true;
    }
    return false;
}

void EqDisplay::timerCallback()
{
    stopTimer();
    if (wheelEditOpen)
    {
        wheelEditOpen = false;
        model.endEdit();
    }
}

//==============================================================================
std::vector<int> EqDisplay::menuTargets (int band) const
{
    std::vector<int> targets;
    if (model.isSelected (band))
    {
        for (int b = 0; b < kNumBands; ++b)
            if (model.isSelected (b) && model.band (b).used)
                targets.push_back (b);
    }
    if (targets.empty())
        targets.push_back (band);
    return targets;
}

void EqDisplay::showBandMenu (int band)
{
    const auto targets = menuTargets (band);
    const auto v = model.band (band);
    juce::Component::SafePointer<EqDisplay> safe (this);

    // Every menu action is one undo step applied to all targeted bands.
    auto forTargets = [safe, targets] (const juce::String& name, std::function<void (BandModel&, int)> fn) {
        return [safe, targets, name, fn] {
            if (safe == nullptr)
                return;
            BandModel::ScopedEdit edit (safe->model, name);
            for (int b : targets)
                fn (safe->model, b);
        };
    };

    juce::PopupMenu menu;
    auto title = "Band " + juce::String (band + 1);
    if (targets.size() > 1)
        title << "  (+" << static_cast<int> (targets.size() - 1) << " selected)";
    menu.addSectionHeader (title);

    juce::PopupMenu shapes;
    for (int i = 0; i < plugin::labels::shapes().size(); ++i)
    {
        const auto shape = static_cast<Shape> (i);
        shapes.addItem (plugin::labels::shapes()[i], true, v.shape == shape,
                        forTargets ("Change Shape", [shape] (BandModel& m, int b) { m.setShape (b, shape); }));
    }
    menu.addSubMenu ("Shape", shapes);

    if (shapeUsesSlope (v.shape))
    {
        juce::PopupMenu slopes;
        for (auto slope : slopesFor (v.shape))
            slopes.addItem (plugin::labels::slopes()[static_cast<int> (slope)], true, v.slope == slope,
                            forTargets ("Change Slope", [slope] (BandModel& m, int b) {
                                m.setValue (b, BandParam::Slope, static_cast<float> (nearestAllowedSlope (m.band (b).shape, slope)));
                            }));
        menu.addSubMenu ("Slope", slopes);
    }
    else if (shapeUsesQ (v.shape))
    {
        juce::PopupMenu qs;
        for (float q : { 0.5f, 0.71f, 1.0f, 1.5f, 2.0f, 4.0f, 8.0f, 16.0f })
            qs.addItem ("Q " + qText (q), true, std::abs (v.q - q) < 0.005f,
                        forTargets ("Change Q", [q] (BandModel& m, int b) { m.setValue (b, BandParam::Q, q); }));
        menu.addSubMenu ("Q", qs);
    }

    juce::PopupMenu modes;
    for (int i = 0; i < plugin::labels::bandModes().size(); ++i)
    {
        const auto mode = static_cast<BandMode> (i);
        const bool allowed = mode == BandMode::Static || shapeUsesGain (v.shape);
        modes.addItem (plugin::labels::bandModes()[i], allowed, v.mode == mode,
                       forTargets ("Change Mode", [mode] (BandModel& m, int b) { m.setMode (b, mode); }));
    }
    menu.addSubMenu ("Mode", modes);

    juce::PopupMenu targetMenu;
    for (int i = 0; i < plugin::labels::targets().size(); ++i)
    {
        const auto target = static_cast<StereoComponent> (i);
        // Without channel pairs (mono, discrete) only All is meaningful (research §4).
        const bool allowed = target == StereoComponent::All || numPairs > 0;
        targetMenu.addItem (plugin::labels::targets()[i], allowed, v.target == target,
                            forTargets ("Change Target", [target] (BandModel& m, int b) {
                                m.setValue (b, BandParam::Target, static_cast<float> (target));
                            }));
    }
    menu.addSubMenu ("Target", targetMenu);

    juce::PopupMenu characters;
    for (int i = 0; i < plugin::labels::characters().size(); ++i)
    {
        const auto character = static_cast<Character> (i);
        characters.addItem (plugin::labels::characters()[i], true, v.character == character,
                            forTargets ("Change Character", [character] (BandModel& m, int b) {
                                m.setValue (b, BandParam::Character, static_cast<float> (character));
                            }));
    }
    menu.addSubMenu ("Character", characters);

    menu.addItem ("Auto Gain", true, v.autoGain, forTargets ("Toggle Auto Gain", [on = ! v.autoGain] (BandModel& m, int b) {
                      m.setValue (b, BandParam::AutoGain, on ? 1.0f : 0.0f);
                  }));
    menu.addSeparator();
    menu.addItem ("Bypass", true, ! v.on,
                  forTargets ("Toggle Band", [on = ! v.on] (BandModel& m, int b) { m.setValue (b, BandParam::On, on ? 1.0f : 0.0f); }));
    menu.addItem ("Solo", v.on, model.getSoloBand() == band, [safe, band] {
        if (safe != nullptr)
            safe->model.setSoloBand (safe->model.getSoloBand() == band ? -1 : band);
    });
    menu.addSeparator();
    menu.addItem ("Copy", [safe, band] {
        if (safe != nullptr)
            safe->model.copyBand (band);
    });
    menu.addItem ("Paste", model.canPaste(), false, forTargets ("Paste Band", [] (BandModel& m, int b) { m.pasteToBand (b); }));
    menu.addItem ("Reset", forTargets ("Reset Band", [] (BandModel& m, int b) { m.resetBand (b); }));
    menu.addItem ("Delete", forTargets ("Delete Band", [] (BandModel& m, int b) { m.deleteBand (b); }));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

//==============================================================================
void EqDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::displayTop, 0.0f, 0.0f, colours::displayBottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    paintGrid (g);

    if (hasFrame)
    {
        const auto& frame = analyzer.frames().read();
        if (frame.numPoints > 1)
        {
            paintSpectrum (g, frame, false);
            paintSpectrum (g, frame, true);
        }
    }

    paintBandCurves (g);
    paintGhost (g);
    paintComposite (g);
    paintNodes (g);
    paintHoverLabel (g);
    paintHint (g);

    if (! lasso.isEmpty())
    {
        g.setColour (colours::accent.withAlpha (0.08f));
        g.fillRect (lasso);
        g.setColour (colours::accent.withAlpha (0.6f));
        g.drawRect (lasso, 1.0f);
    }
}

void EqDisplay::paintGrid (juce::Graphics& g) const
{
    const auto w = static_cast<float> (getWidth());
    const auto h = static_cast<float> (getHeight());
    g.setFont (uiFont (11.0f));

    static constexpr int lines[] = { 20,  30,  40,   50,   60,   80,   100,  200,  300,  400,   500,
                                     600, 800, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 20000 };
    for (auto hz : lines)
    {
        const bool major = hz == 100 || hz == 1000 || hz == 10000;
        g.setColour (major ? colours::gridStrong : colours::grid);
        g.drawVerticalLine (juce::roundToInt (xForFrequency (static_cast<float> (hz))), 0.0f, h);
    }

    static constexpr std::pair<float, const char*> labels[] = { { 20.0f, "20" },    { 50.0f, "50" },   { 100.0f, "100" },
                                                                { 200.0f, "200" },  { 500.0f, "500" }, { 1000.0f, "1k" },
                                                                { 2000.0f, "2k" },  { 5000.0f, "5k" }, { 10000.0f, "10k" },
                                                                { 20000.0f, "20k" } };
    g.setColour (colours::textDim);
    for (const auto& [hz, text] : labels)
        g.drawText (text, juce::Rectangle<float> (xForFrequency (hz) - 20.0f, h - 16.0f, 40.0f, 14.0f), juce::Justification::centred);

    const int range = juce::roundToInt (eqRangeDb);
    const int step = gridStepFor (eqRangeDb);
    for (int db = -range; db <= range; db += step)
    {
        const float y = yForEqDb (static_cast<float> (db));
        g.setColour (db == 0 ? colours::gridStrong.brighter (0.2f) : colours::grid);
        g.drawHorizontalLine (juce::roundToInt (y), 0.0f, w);
        if (db != 0)
        {
            g.setColour (colours::textDim);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (w - 30.0f, y - 7.0f, 26.0f, 14.0f),
                        juce::Justification::centredRight);
        }
    }
}

void EqDisplay::paintSpectrum (juce::Graphics& g, const plugin::SpectrumFrame& frame, bool post) const
{
    const auto h = static_cast<float> (getHeight());
    juce::Path line;
    bool started = false;

    for (int i = 0; i < frame.numPoints; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        const float hz = frame.hz[idx];
        if (hz < kMinHz || hz > kMaxHz)
            continue;

        const float x = xForFrequency (hz);
        const float y = juce::jlimit (0.0f, h, yForAnalyzerDb (post ? frame.postDb[idx] : frame.preDb[idx]));
        if (! started)
        {
            line.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            line.lineTo (x, y);
        }
    }

    if (! started)
        return;

    juce::Path fill (line);
    fill.lineTo (line.getCurrentPosition().x, h);
    fill.lineTo (line.getBounds().getX(), h);
    fill.closeSubPath();

    if (post)
    {
        g.setGradientFill (juce::ColourGradient (colours::spectrumPost.withAlpha (0.16f), 0.0f, 0.0f,
                                                 colours::spectrumPost.withAlpha (0.02f), 0.0f, h, false));
        g.fillPath (fill);
        g.setColour (colours::spectrumPost.withAlpha (0.55f));
        g.strokePath (line, juce::PathStrokeType (1.1f));
    }
    else
    {
        g.setColour (colours::spectrumPre.withAlpha (0.55f));
        g.fillPath (fill);
    }
}

juce::Path EqDisplay::curvePath (const std::vector<float>& db) const
{
    juce::Path path;
    const auto h = static_cast<float> (getHeight());
    const int n = juce::jmin (curves.numValidPoints(), static_cast<int> (db.size()));
    for (int i = 0; i < n; ++i)
    {
        const float x = xForFrequency (curves.frequency (i));
        const float y = juce::jlimit (-20.0f, h + 20.0f, yForEqDb (db[static_cast<std::size_t> (i)]));
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }
    return path;
}

void EqDisplay::paintBandCurves (juce::Graphics& g) const
{
    const float zeroY = yForEqDb (0.0f);
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! curves.hasBand (b))
            continue;

        const auto path = curvePath (curves.bandDb (b));
        if (path.isEmpty())
            continue;

        const bool emphasised = model.isSelected (b) || b == hoveredBand || b == dragBand;
        const auto colour = bandColour (b);

        juce::Path fill (path);
        fill.lineTo (path.getCurrentPosition().x, zeroY);
        fill.lineTo (path.getBounds().getX(), zeroY);
        fill.closeSubPath();
        g.setColour (colour.withAlpha (emphasised ? 0.20f : 0.07f));
        g.fillPath (fill);

        if (emphasised)
        {
            g.setColour (colour.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.2f));
        }

        // Dynamic bands: the region between the static and the current curve (DYN-11).
        if (curves.hasDynamic (b))
        {
            const auto dynamicPath = curvePath (curves.dynamicDb (b));
            juce::Path region;
            const auto& staticDb = curves.bandDb (b);
            const auto& dynamicDb = curves.dynamicDb (b);
            const auto h = static_cast<float> (getHeight());
            const int n = juce::jmin (curves.numValidPoints(), static_cast<int> (staticDb.size()));
            for (int i = 0; i < n; ++i)
            {
                const auto p = juce::Point<float> (xForFrequency (curves.frequency (i)),
                                                   juce::jlimit (-20.0f, h + 20.0f, yForEqDb (staticDb[static_cast<std::size_t> (i)])));
                if (i == 0)
                    region.startNewSubPath (p);
                else
                    region.lineTo (p);
            }
            for (int i = n - 1; i >= 0; --i)
                region.lineTo (xForFrequency (curves.frequency (i)),
                               juce::jlimit (-20.0f, h + 20.0f, yForEqDb (dynamicDb[static_cast<std::size_t> (i)])));
            region.closeSubPath();
            g.setColour (colour.withAlpha (0.35f));
            g.fillPath (region);
            g.setColour (colour.withAlpha (0.95f));
            g.strokePath (dynamicPath, juce::PathStrokeType (1.4f));
        }
    }
}

void EqDisplay::paintComposite (juce::Graphics& g) const
{
    const auto path = curvePath (curves.compositeDb());
    g.setColour (colours::curve);
    g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EqDisplay::paintGhost (juce::Graphics& g) const
{
    if (! ghost.has_value() || dragMode != DragMode::None)
        return;

    const auto path = curvePath (ghost->curve);
    juce::Path dashed;
    const float dashes[] = { 4.0f, 4.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, path, dashes, 2);
    g.setColour (colours::text.withAlpha (0.28f));
    g.fillPath (dashed);

    const auto h = static_cast<float> (getHeight());
    const juce::Point<float> centre { xForFrequency (static_cast<float> (ghost->shape.frequencyHz)),
                                      juce::jlimit (kNodeRadius + 1.0f, h - kNodeRadius - 1.0f,
                                                    yForEqDb (nodeDbFor (ghost->shape, curves))) };
    const auto circle = juce::Rectangle<float> (kNodeRadius * 2.0f, kNodeRadius * 2.0f).withCentre (centre);
    g.setColour (colours::text.withAlpha (0.45f));
    g.drawEllipse (circle, 1.2f);
    g.setFont (uiFont (11.0f));
    g.drawText (plugin::labels::shapes()[static_cast<int> (ghost->shape.shape)],
                circle.withSizeKeepingCentre (110.0f, 14.0f).translated (0.0f, -kNodeRadius - 10.0f), juce::Justification::centred);
}

void EqDisplay::paintNodes (juce::Graphics& g) const
{
    // Unselected first, then selected, then the hovered/dragged node on top.
    std::vector<int> order;
    for (int pass = 0; pass < 3; ++pass)
        for (int b = 0; b < kNumBands; ++b)
        {
            if (! model.band (b).used)
                continue;
            const bool top = b == hoveredBand || b == dragBand;
            const int rank = top ? 2 : (model.isSelected (b) ? 1 : 0);
            if (rank == pass)
                order.push_back (b);
        }

    g.setFont (uiFont (10.0f, true));
    for (int b : order)
    {
        const auto& v = model.band (b);
        const bool selected = model.isSelected (b);
        const bool highlighted = selected || b == hoveredBand || b == dragBand;
        const float radius = kNodeRadius + (highlighted ? 1.5f : 0.0f);
        const auto circle = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (nodePosition (b));

        const auto colour = v.on ? bandColour (b) : colours::powerOff;
        g.setColour (colour.withAlpha (v.on ? 0.95f : 0.7f));
        g.fillEllipse (circle);
        if (selected)
        {
            g.setColour (colours::text);
            g.drawEllipse (circle.expanded (2.0f), 1.5f);
        }
        if (model.getSoloBand() == b)
        {
            g.setColour (colours::accent);
            g.drawEllipse (circle.expanded (4.5f), 1.5f);
        }
        g.setColour (colours::background);
        g.drawText (juce::String (b + 1), circle, juce::Justification::centred);
    }
}

void EqDisplay::paintHoverLabel (juce::Graphics& g) const
{
    const int band = dragBand >= 0 ? dragBand : hoveredBand;
    if (band < 0 || ! model.band (band).used)
        return;

    const auto& v = model.band (band);
    juce::String text = plugin::labels::shapes()[static_cast<int> (v.shape)] + "   " + plugin::format::frequency (v.frequency);
    if (shapeUsesGain (v.shape))
        text << "   " << plugin::format::decibels (v.gain);
    if (shapeUsesSlope (v.shape))
        text << "   " << plugin::labels::slopes()[static_cast<int> (v.slope)];
    if (shapeUsesQ (v.shape) && (v.shape != Shape::LowCut && v.shape != Shape::HighCut))
        text << "   Q " << qText (v.q);
    if (! v.on)
        text << "   (bypassed)";

    const auto font = uiFont (12.0f);
    const float width = juce::GlyphArrangement::getStringWidth (font, text) + 16.0f;
    const auto node = nodePosition (band);
    auto box = juce::Rectangle<float> (width, 22.0f).withCentre (node).translated (0.0f, -kNodeRadius - 20.0f);
    if (box.getY() < 2.0f)
        box = box.withY (node.y + kNodeRadius + 8.0f);
    box = box.withX (juce::jlimit (2.0f, static_cast<float> (getWidth()) - width - 2.0f, box.getX()));

    g.setColour (colours::panel.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (bandColour (band).withAlpha (0.8f));
    g.drawRoundedRectangle (box, 4.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (font);
    g.drawText (text, box, juce::Justification::centred);
}

void EqDisplay::paintHint (juce::Graphics& g) const
{
    if (model.numUsedBands() > 0)
        return;
    g.setColour (colours::textDim);
    g.setFont (uiFont (12.0f));
    g.drawText (juce::String::fromUTF8 ("Double-click to add a band   \xc2\xb7   drag the curve to add a bell or shelf   \xc2\xb7   "
                                        "right-click a band for options"),
                getLocalBounds().removeFromTop (28), juce::Justification::centred);
}
} // namespace ssc::ui
