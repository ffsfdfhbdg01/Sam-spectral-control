#pragma once

#include <vector>

#include "ssc/engine/Types.h"

/**
    Pure (JUCE-free) rules of band editing, shared by the display, menus and panel and covered by
    unit tests (research §5c, BAND-4/BAND-5).
*/
namespace ssc::ui
{
/** How a band is being created. */
enum class CreateGesture
{
    DoubleClick, ///< double click (or hover preview) on the background
    DragCurve    ///< press on the composite curve and drag
};

/** Zone thresholds in normalised display coordinates (x: log frequency 0…1, y: 0 top … 1 bottom). */
namespace zones
{
    inline constexpr float lowCutX = 0.04f;
    inline constexpr float highCutX = 0.96f;
    inline constexpr float notchY = 0.88f;
    inline constexpr float lowShelfX = 0.15f;
    inline constexpr float highShelfX = 0.85f;
} // namespace zones

/** Shape of a band created at (x, y) with the given gesture (BAND-4). */
Shape shapeForGesture (float x, float y, CreateGesture gesture) noexcept;

/** True if the shape has a gain parameter (and can therefore be dynamic or spectral). */
bool shapeUsesGain (Shape shape) noexcept;

/** True if the slope parameter changes the shape (otherwise Q is shown instead). */
bool shapeUsesSlope (Shape shape) noexcept;

/** True if the shape has a meaningful Q. */
bool shapeUsesQ (Shape shape) noexcept;

/** Slopes offered for a shape, in menu order (empty when the shape has no slope). */
std::vector<Slope> slopesFor (Shape shape);

/** The slope closest to `slope` that the shape supports (the slope itself if allowed). */
Slope nearestAllowedSlope (Shape shape, Slope slope) noexcept;

/** Defaults for a newly created band of the given shape. */
float defaultQFor (Shape shape) noexcept;
Slope defaultSlopeFor (Shape shape) noexcept;
} // namespace ssc::ui
