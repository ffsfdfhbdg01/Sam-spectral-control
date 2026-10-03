#pragma once

#include <cstdint>

/**
    Enumerations shared by the DSP engine and the plugin parameters.
    The numeric order of every enum is part of the saved state: append new values at the end
    and never reorder existing ones.
*/
namespace ssc
{
inline constexpr int kNumBands = 24;
inline constexpr int kMaxChannels = 32;

enum class Shape : std::uint8_t
{
    Bell,
    LowShelf,
    HighShelf,
    LowCut,
    HighCut,
    Notch,
    BandPass,
    TiltShelf,
    FlatTilt,
    AllPass
};

enum class Slope : std::uint8_t
{
    Db6,
    Db12,
    Db18,
    Db24,
    Db30,
    Db36,
    Db48,
    Db72,
    Db96,
    Brickwall
};

enum class BandMode : std::uint8_t
{
    Static,
    Dynamic,
    Spectral
};

enum class StereoComponent : std::uint8_t
{
    All,
    Left,
    Right,
    Mid,
    Side
};

enum class Character : std::uint8_t
{
    Clear,
    Warm,
    Sharp
};

enum class ThresholdMode : std::uint8_t
{
    Absolute,
    Relative
};

enum class Direction : std::uint8_t
{
    Above,
    Below
};

enum class DetectorLink : std::uint8_t
{
    Independent,
    Pair,
    Group,
    All
};

enum class PhaseMode : std::uint8_t
{
    MinimumPhase,
    NaturalPhase,
    AnalogFeel
};

enum class AnalogModel : std::uint8_t
{
    N,
    P,
    S
};

enum class Quality : std::uint8_t
{
    Eco,
    Normal,
    High,
    Max
};

enum class Oversampling : std::uint8_t
{
    Off,
    X2,
    X4,
    X8
};

enum class AutoGainWeighting : std::uint8_t
{
    Fletcher,
    Digital
};

enum class AutoGainSpeed : std::uint8_t
{
    VeryFast,
    Fast,
    Medium,
    Long,
    Stable
};

constexpr int slopeDbPerOctave (Slope s) noexcept
{
    switch (s)
    {
        case Slope::Db6: return 6;
        case Slope::Db12: return 12;
        case Slope::Db18: return 18;
        case Slope::Db24: return 24;
        case Slope::Db30: return 30;
        case Slope::Db36: return 36;
        case Slope::Db48: return 48;
        case Slope::Db72: return 72;
        case Slope::Db96: return 96;
        case Slope::Brickwall: return 0;
    }
    return 12;
}

constexpr int oversamplingFactor (Oversampling os) noexcept
{
    switch (os)
    {
        case Oversampling::Off: return 1;
        case Oversampling::X2: return 2;
        case Oversampling::X4: return 4;
        case Oversampling::X8: return 8;
    }
    return 1;
}

/** Shapes that have a gain parameter and can therefore run in Dynamic or Spectral mode. */
constexpr bool shapeHasGain (Shape s) noexcept
{
    return s == Shape::Bell || s == Shape::LowShelf || s == Shape::HighShelf || s == Shape::TiltShelf || s == Shape::FlatTilt;
}
} // namespace ssc
