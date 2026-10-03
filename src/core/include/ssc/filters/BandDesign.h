#pragma once

#include <array>
#include <complex>
#include <functional>

#include "ssc/engine/Types.h"
#include "ssc/filters/Sections.h"

namespace ssc::filters
{
inline constexpr int kMaxSections = 8;

/** Shape parameters of one EQ band (Character/dynamics are applied elsewhere). */
struct BandShape
{
    Shape shape = Shape::Bell;
    double frequencyHz = 1000.0;
    double gainDb = 0.0;
    double q = 1.0;
    Slope slope = Slope::Db12;

    /** Exact comparison (used to detect parameter changes, not as a numerical tolerance test). */
    bool operator== (const BandShape& other) const noexcept
    {
        constexpr std::equal_to<double> same;
        return shape == other.shape && same (frequencyHz, other.frequencyHz) && same (gainDb, other.gainDb) && same (q, other.q)
            && slope == other.slope;
    }
};

/** Analog prototype and its matched digital realisation, as a cascade of sections. */
struct BandDesign
{
    std::array<AnalogSection, kMaxSections> analog {};
    std::array<Biquad, kMaxSections> digital {};
    std::array<SvfParams, kMaxSections> svf {};
    int numSections = 0;

    /** Topology key: changes only when the number or order of sections changes. */
    int topology() const noexcept
    {
        int key = numSections;
        for (int i = 0; i < numSections; ++i)
            key = key * 2 + (digital[static_cast<std::size_t> (i)].firstOrder ? 1 : 0);
        return key;
    }

    std::complex<double> analogResponse (double hz) const noexcept;
    std::complex<double> digitalResponse (double hz, double sampleRate) const noexcept;
};

/** Number of 6 dB/oct steps for a slope (Brickwall maps to 96 dB/oct in minimum phase). */
int slopeOrder (Slope slope) noexcept;

/** Designs the analog prototype and the matched digital cascade for a band. */
BandDesign designBand (const BandShape& shape, double sampleRate);

/** True if the band, as configured, has no effect (e.g. a bell with 0 dB gain). */
bool isIdentity (const BandShape& shape) noexcept;
} // namespace ssc::filters
