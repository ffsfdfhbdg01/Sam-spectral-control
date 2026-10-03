#include "ui/ResponseCurves.h"

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace ssc::ui
{
namespace
{
    float toDb (std::complex<double> h) noexcept
    {
        const double magnitude = std::abs (h);
        return magnitude > 0.0 ? std::max (ResponseCurves::kFloorDb, static_cast<float> (20.0 * std::log10 (magnitude)))
                               : ResponseCurves::kFloorDb;
    }
} // namespace

void ResponseCurves::setGrid (int numPoints, float minHz, float maxHz)
{
    numPoints = std::max (2, numPoints);
    hz.resize (static_cast<std::size_t> (numPoints));
    for (int i = 0; i < numPoints; ++i)
        hz[static_cast<std::size_t> (i)] = minHz * std::pow (maxHz / minHz, static_cast<float> (i) / static_cast<float> (numPoints - 1));

    validPoints = static_cast<int> (std::count_if (hz.begin(), hz.end(), [this] (float f) { return f < 0.5 * fs; }));
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& shape = shapes[static_cast<std::size_t> (b)];
        auto& curve = curves[static_cast<std::size_t> (b)];
        curve.assign (hz.size(), 0.0f);
        if (shape.has_value())
            compute (*shape, curve);
    }
    updateComposite();
}

void ResponseCurves::setSampleRate (double sampleRate)
{
    if (sampleRate <= 0.0 || juce::exactlyEqual (sampleRate, fs))
        return;
    fs = sampleRate;
    if (! hz.empty())
        setGrid (static_cast<int> (hz.size()), hz.front(), hz.back());
}

bool ResponseCurves::updateBand (int band, const std::optional<filters::BandShape>& shape)
{
    auto& current = shapes[static_cast<std::size_t> (band)];
    if (current == shape)
        return false;

    current = shape;
    auto& curve = curves[static_cast<std::size_t> (band)];
    curve.assign (hz.size(), 0.0f);
    if (shape.has_value())
        compute (*shape, curve);
    return true;
}

void ResponseCurves::updateComposite()
{
    composite.assign (hz.size(), 0.0f);
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! shapes[static_cast<std::size_t> (b)].has_value())
            continue;
        const auto& curve = curves[static_cast<std::size_t> (b)];
        for (std::size_t i = 0; i < composite.size(); ++i)
            composite[i] = std::max (kFloorDb, composite[i] + curve[i]);
    }
}

std::vector<float> ResponseCurves::responseOf (const filters::BandShape& shape) const
{
    std::vector<float> out (hz.size(), 0.0f);
    compute (shape, out);
    return out;
}

float ResponseCurves::responseAt (const filters::BandShape& shape, float frequencyHz) const
{
    if (filters::isIdentity (shape))
        return 0.0f;
    const auto design = filters::designBand (shape, fs);
    return toDb (design.digitalResponse (std::min (static_cast<double> (frequencyHz), 0.4999 * fs), fs));
}

void ResponseCurves::compute (const filters::BandShape& shape, std::vector<float>& out) const
{
    std::fill (out.begin(), out.end(), 0.0f);
    if (filters::isIdentity (shape))
        return;

    const auto design = filters::designBand (shape, fs);
    for (int i = 0; i < validPoints; ++i)
        out[static_cast<std::size_t> (i)] = toDb (design.digitalResponse (hz[static_cast<std::size_t> (i)], fs));

    // Above Nyquist the digital filter has no response; hold the last value so curves end flat.
    const float last = validPoints > 0 ? out[static_cast<std::size_t> (validPoints - 1)] : 0.0f;
    for (std::size_t i = static_cast<std::size_t> (validPoints); i < out.size(); ++i)
        out[i] = last;
}
} // namespace ssc::ui
