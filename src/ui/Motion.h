#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Interface motion (research §15 d9): cubic Bézier easings, values that glide to a target and
    can be retargeted at any moment, and the Full / Reduced / Off setting. Only the frame of the
    interface moves (panels, drawers, indicators); curves and values are always drawn at once.
*/
namespace ssc::ui::motion
{
/** How much the interface moves. Reduced: no slides, cross-fades of at most 100 ms. Off: no motion. */
enum class Level
{
    Full,
    Reduced,
    Off
};

/** CSS-style cubic Bézier from (0, 0) to (1, 1) with control points (x1, y1) and (x2, y2). */
struct Easing
{
    float x1, y1, x2, y2;

    /** Eased progress for a time fraction t (clamped to 0…1). */
    float operator() (float t) const noexcept;
};

inline constexpr Easing decelerate { 0.0f, 0.0f, 0.2f, 1.0f }; ///< elements entering the screen
inline constexpr Easing accelerate { 0.4f, 0.0f, 1.0f, 1.0f }; ///< elements leaving the screen
inline constexpr Easing standard { 0.2f, 0.0f, 0.0f, 1.0f };   ///< elements moving within the screen

/** A value gliding to its target. Retargeting starts from the current value, so motion can be
    interrupted at any time and never blocks input. */
class Tween
{
public:
    /** Jumps to `value` (no motion). */
    void snap (float value) noexcept;

    /** Glides from the current value to `target`; a running glide to the same target continues. */
    void animateTo (float target, double nowMs, double durationMs, Easing easing) noexcept;

    float valueAt (double nowMs) const noexcept;
    bool isRunning (double nowMs) const noexcept;
    float getTarget() const noexcept { return to; }

private:
    float from = 0.0f, to = 0.0f;
    double start = 0.0, duration = 0.0;
    Easing easing = standard;
};

/** Duration of a slide or resize at a level: Full as given, otherwise instant. */
double moveDuration (Level level, double fullMs) noexcept;

/** Duration of a fade at a level: Full as given, Reduced at most 100 ms, Off instant. */
double fadeDuration (Level level, double fullMs) noexcept;

/** Clock of all animations (ms). */
double now() noexcept;

/** The level for a component: set on an ancestor with setLevel() (the editor), Full otherwise. */
Level levelFor (const juce::Component& component);
void setLevel (juce::Component& root, Level level);

/** Display names, in enum order. */
const juce::StringArray& levelNames();

/**
    Moves and fades one component towards target bounds and opacity, at 60 frames per second
    while something moves and without a timer otherwise. The component is hidden once fully
    transparent.
*/
class ComponentMotion
{
public:
    explicit ComponentMotion (juce::Component& component);

    /** Glides to `bounds` (position and size) in `durationMs`. */
    void moveTo (juce::Rectangle<int> bounds, double durationMs, Easing easing);

    /** Fades to `alpha` (0 hides the component at the end) in `durationMs`. */
    void fadeTo (float alpha, double durationMs, Easing easing);

    /** Jumps to the bounds and opacity. */
    void snapTo (juce::Rectangle<int> bounds, float alpha);

    juce::Rectangle<int> getTargetBounds() const;
    float getTargetAlpha() const noexcept { return alpha.getTarget(); }
    bool isAnimating() const;

    /** Called after every frame (e.g. to move attached components). */
    std::function<void()> onFrame;

private:
    void frame();
    void apply (double nowMs);

    juce::Component& component;
    Tween x, y, w, h, alpha;
    juce::TimedCallback timer { [this] { frame(); } };
};
} // namespace ssc::ui::motion
