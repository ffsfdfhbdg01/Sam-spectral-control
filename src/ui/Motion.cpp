#include "ui/Motion.h"

#include <cmath>

namespace ssc::ui::motion
{
namespace
{
    const juce::Identifier kLevelProperty { "sscMotionLevel" };
}

float Easing::operator() (float t) const noexcept
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    // Bézier polynomials with P0 = (0, 0) and P3 = (1, 1); solve x(s) = t, return y(s).
    const double cx = 3.0 * x1, bx = 3.0 * (x2 - x1) - cx, ax = 1.0 - cx - bx;
    const double cy = 3.0 * y1, by = 3.0 * (y2 - y1) - cy, ay = 1.0 - cy - by;
    auto sampleX = [=] (double s) { return ((ax * s + bx) * s + cx) * s; };
    auto sampleY = [=] (double s) { return ((ay * s + by) * s + cy) * s; };
    auto slopeX = [=] (double s) { return (3.0 * ax * s + 2.0 * bx) * s + cx; };

    const double target = t;
    double s = target;
    for (int i = 0; i < 8; ++i)
    {
        const double error = sampleX (s) - target;
        if (std::abs (error) < 1.0e-7)
            return static_cast<float> (sampleY (s));
        const double slope = slopeX (s);
        if (std::abs (slope) < 1.0e-6)
            break;
        s -= error / slope;
        if (s < 0.0 || s > 1.0)
            break;
    }

    // Bisection: x(s) is monotonic for control points inside 0…1.
    double lo = 0.0, hi = 1.0;
    s = target;
    for (int i = 0; i < 48; ++i)
    {
        const double x = sampleX (s);
        if (std::abs (x - target) < 1.0e-7)
            break;
        (x < target ? lo : hi) = s;
        s = 0.5 * (lo + hi);
    }
    return static_cast<float> (sampleY (s));
}

void Tween::snap (float value) noexcept
{
    from = to = value;
    duration = 0.0;
}

void Tween::animateTo (float target, double nowMs, double durationMs, Easing e) noexcept
{
    if (juce::exactlyEqual (target, to))
        return; // already there or on the way
    from = valueAt (nowMs);
    to = target;
    start = nowMs;
    duration = durationMs > 0.0 ? durationMs : 0.0;
    easing = e;
}

float Tween::valueAt (double nowMs) const noexcept
{
    if (duration <= 0.0)
        return to;
    const double t = (nowMs - start) / duration;
    if (t >= 1.0)
        return to;
    if (t <= 0.0)
        return from;
    return from + (to - from) * easing (static_cast<float> (t));
}

bool Tween::isRunning (double nowMs) const noexcept
{
    return duration > 0.0 && nowMs < start + duration;
}

double moveDuration (Level level, double fullMs) noexcept
{
    return level == Level::Full ? fullMs : 0.0;
}

double fadeDuration (Level level, double fullMs) noexcept
{
    switch (level)
    {
        case Level::Full: return fullMs;
        case Level::Reduced: return std::min (fullMs, 100.0);
        case Level::Off: break;
    }
    return 0.0;
}

double now() noexcept
{
    return juce::Time::getMillisecondCounterHiRes();
}

Level levelFor (const juce::Component& component)
{
    for (auto* c = &component; c != nullptr; c = c->getParentComponent())
        if (const auto* value = c->getProperties().getVarPointer (kLevelProperty))
            return static_cast<Level> (juce::jlimit (0, 2, static_cast<int> (*value)));
    return Level::Full;
}

void setLevel (juce::Component& root, Level level)
{
    root.getProperties().set (kLevelProperty, static_cast<int> (level));
}

const juce::StringArray& levelNames()
{
    static const juce::StringArray names { "Full", "Reduced", "Off" };
    return names;
}

ComponentMotion::ComponentMotion (juce::Component& c)
    : component (c)
{
    snapTo (c.getBounds(), c.isVisible() ? 1.0f : 0.0f);
}

void ComponentMotion::moveTo (juce::Rectangle<int> bounds, double durationMs, Easing easing)
{
    const double t = now();
    x.animateTo (static_cast<float> (bounds.getX()), t, durationMs, easing);
    y.animateTo (static_cast<float> (bounds.getY()), t, durationMs, easing);
    w.animateTo (static_cast<float> (bounds.getWidth()), t, durationMs, easing);
    h.animateTo (static_cast<float> (bounds.getHeight()), t, durationMs, easing);
    frame();
}

void ComponentMotion::fadeTo (float target, double durationMs, Easing easing)
{
    alpha.animateTo (target, now(), durationMs, easing);
    frame();
}

void ComponentMotion::snapTo (juce::Rectangle<int> bounds, float target)
{
    x.snap (static_cast<float> (bounds.getX()));
    y.snap (static_cast<float> (bounds.getY()));
    w.snap (static_cast<float> (bounds.getWidth()));
    h.snap (static_cast<float> (bounds.getHeight()));
    alpha.snap (target);
    frame();
}

juce::Rectangle<int> ComponentMotion::getTargetBounds() const
{
    return { juce::roundToInt (x.getTarget()), juce::roundToInt (y.getTarget()), juce::roundToInt (w.getTarget()),
             juce::roundToInt (h.getTarget()) };
}

bool ComponentMotion::isAnimating() const
{
    const double t = now();
    return x.isRunning (t) || y.isRunning (t) || w.isRunning (t) || h.isRunning (t) || alpha.isRunning (t);
}

void ComponentMotion::apply (double t)
{
    const juce::Rectangle<int> bounds (juce::roundToInt (x.valueAt (t)), juce::roundToInt (y.valueAt (t)), juce::roundToInt (w.valueAt (t)),
                                       juce::roundToInt (h.valueAt (t)));
    if (bounds != component.getBounds())
        component.setBounds (bounds);
    const float a = juce::jlimit (0.0f, 1.0f, alpha.valueAt (t));
    const bool visible = a > 0.001f;
    if (! juce::exactlyEqual (component.getAlpha(), visible ? a : 1.0f))
        component.setAlpha (visible ? a : 1.0f); // a hidden component keeps full opacity for the next show
    if (component.isVisible() != visible)
        component.setVisible (visible);
}

void ComponentMotion::frame()
{
    apply (now());
    if (onFrame)
        onFrame();
    if (isAnimating())
    {
        if (! timer.isTimerRunning())
            timer.startTimerHz (60);
    }
    else
    {
        timer.stopTimer();
    }
}
} // namespace ssc::ui::motion
