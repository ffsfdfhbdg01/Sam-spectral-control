#include "ssc/engine/GainTable.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "ssc/filters/SvfSection.h"

namespace ssc::engine
{
namespace
{
    constexpr double kMinMagnitudeDb = 1.0e-3;
} // namespace

filters::BandShape withNonZeroGain (filters::BandShape shape) noexcept
{
    if (std::abs (shape.gainDb) < kMinMagnitudeDb)
        shape.gainDb = std::signbit (shape.gainDb) ? -kMinMagnitudeDb : kMinMagnitudeDb;
    return shape;
}

void GainTable::prepare (double sampleRate)
{
    fs = sampleRate;
    entries.assign (static_cast<std::size_t> (kNumEntries), Entry {});
    generation = 1;
    designs = 0;
}

void GainTable::setShape (const filters::BandShape& s) noexcept
{
    constexpr std::equal_to<double> same;
    const bool changed =
        s.shape != shape.shape || s.slope != shape.slope || ! same (s.frequencyHz, shape.frequencyHz) || ! same (s.q, shape.q);
    if (! changed)
        return;

    shape = s;
    shape.gainDb = 0.0;
    if (++generation == 0) // wrapped: make sure no stale entry looks valid
    {
        for (auto& e : entries)
            e.generation = 0;
        generation = 1;
    }
}

const GainTable::Entry& GainTable::entry (int index) noexcept
{
    auto& e = entries[static_cast<std::size_t> (index)];
    if (e.generation != generation)
    {
        auto s = shape;
        s.gainDb = kMinDb + kStepDb * index;
        const auto design = filters::designBand (withNonZeroGain (s), fs);
        e.params = design.svf;
        e.generation = generation;
        ++designs;
    }
    return e;
}

void GainTable::lookup (double gainDb, int numSections, Params& out) noexcept
{
    const double position = (std::clamp (gainDb, kMinDb, kMaxDb) - kMinDb) / kStepDb;
    const int i0 = std::min (static_cast<int> (position), kNumEntries - 2);
    const double t = position - i0;
    const auto& a = entry (i0).params;
    const auto& b = entry (i0 + 1).params;
    for (int s = 0; s < numSections; ++s)
        out[static_cast<std::size_t> (s)] = filters::lerp (a[static_cast<std::size_t> (s)], b[static_cast<std::size_t> (s)], t);
}
} // namespace ssc::engine
