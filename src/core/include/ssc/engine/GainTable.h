#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "ssc/filters/BandDesign.h"

namespace ssc::engine
{
/**
    SVF parameters of a band as a function of its total gain, for Dynamic bands (research §6c).

    Frequency, Q, shape and slope are fixed per table. Entries (0.25 dB apart, ±60 dB) are designed
    lazily with the same matched design as a static band at that gain and interpolated linearly per
    sample, so the static response of a Dynamic band equals that of a Static band. Changing the
    shape invalidates every entry in O(1). Real-time safe after prepare().
*/
class GainTable
{
public:
    static constexpr double kMinDb = -60.0;
    static constexpr double kMaxDb = 60.0;
    static constexpr double kStepDb = 0.25;
    static constexpr int kNumEntries = static_cast<int> ((kMaxDb - kMinDb) / kStepDb) + 1;

    using Params = std::array<filters::SvfParams, filters::kMaxSections>;

    void prepare (double sampleRate);

    /** Sets shape, frequency, Q and slope (the gain is ignored); invalidates entries when they change. */
    void setShape (const filters::BandShape& shape) noexcept;

    /** Parameters of the first numSections sections at a total gain. */
    void lookup (double gainDb, int numSections, Params& out) noexcept;

    /** Number of entries designed since prepare() (for tests and benchmarks). */
    std::uint64_t designsComputed() const noexcept { return designs; }

private:
    struct Entry
    {
        Params params {};
        std::uint32_t generation = 0;
    };

    const Entry& entry (int index) noexcept;

    std::vector<Entry> entries;
    filters::BandShape shape;
    double fs = 48000.0;
    std::uint32_t generation = 1;
    std::uint64_t designs = 0;
};

/** The band shape used to design a Dynamic band's topology: its gain, but never exactly 0 dB. */
filters::BandShape withNonZeroGain (filters::BandShape shape) noexcept;
} // namespace ssc::engine
