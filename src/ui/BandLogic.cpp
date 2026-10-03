#include "ui/BandLogic.h"

#include <algorithm>
#include <cstdlib>

namespace ssc::ui
{
Shape shapeForGesture (float x, float y, CreateGesture gesture) noexcept
{
    if (gesture == CreateGesture::DragCurve)
    {
        if (x < zones::lowShelfX)
            return Shape::LowShelf;
        if (x > zones::highShelfX)
            return Shape::HighShelf;
        return Shape::Bell;
    }

    if (x < zones::lowCutX)
        return Shape::LowCut;
    if (x > zones::highCutX)
        return Shape::HighCut;
    if (y > zones::notchY)
        return Shape::Notch;
    return Shape::Bell;
}

bool shapeUsesGain (Shape shape) noexcept
{
    return shapeHasGain (shape);
}

bool shapeUsesSlope (Shape shape) noexcept
{
    return ! slopesFor (shape).empty();
}

bool shapeUsesQ (Shape shape) noexcept
{
    return shape != Shape::FlatTilt;
}

std::vector<Slope> slopesFor (Shape shape)
{
    switch (shape)
    {
        case Shape::LowCut:
        case Shape::HighCut:
            return { Slope::Db6,  Slope::Db12, Slope::Db18, Slope::Db24, Slope::Db30,
                     Slope::Db36, Slope::Db48, Slope::Db72, Slope::Db96, Slope::Brickwall };
        case Shape::LowShelf:
        case Shape::HighShelf:
        case Shape::BandPass: return { Slope::Db6, Slope::Db12, Slope::Db18, Slope::Db24, Slope::Db30, Slope::Db36, Slope::Db48 };
        case Shape::TiltShelf: return { Slope::Db6, Slope::Db12 };
        case Shape::Bell:
        case Shape::Notch:
        case Shape::FlatTilt:
        case Shape::AllPass: break;
    }
    return {};
}

Slope nearestAllowedSlope (Shape shape, Slope slope) noexcept
{
    const auto allowed = slopesFor (shape);
    if (allowed.empty() || std::find (allowed.begin(), allowed.end(), slope) != allowed.end())
        return slope;

    // Brickwall is steeper than any finite slope.
    const auto steepness = [] (Slope s) { return s == Slope::Brickwall ? 1000 : slopeDbPerOctave (s); };
    return *std::min_element (allowed.begin(), allowed.end(), [&] (Slope a, Slope b) {
        return std::abs (steepness (a) - steepness (slope)) < std::abs (steepness (b) - steepness (slope));
    });
}

float defaultQFor (Shape shape) noexcept
{
    switch (shape)
    {
        case Shape::LowCut:
        case Shape::HighCut:
        case Shape::LowShelf:
        case Shape::HighShelf:
        case Shape::TiltShelf: return 0.70710678f;
        case Shape::Notch: return 4.0f;
        case Shape::Bell:
        case Shape::BandPass:
        case Shape::FlatTilt:
        case Shape::AllPass: break;
    }
    return 1.0f;
}

Slope defaultSlopeFor (Shape shape) noexcept
{
    return shape == Shape::LowCut || shape == Shape::HighCut ? Slope::Db24 : Slope::Db12;
}
} // namespace ssc::ui
