#include "ui/ResponseCurves.h"

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

#include "ssc/phase/PhaseCorrector.h"
#include "ssc/util/Math.h"

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
        computeDynamic (b);
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

void ResponseCurves::setResponseModel (const Model& newModel)
{
    auto m = newModel;
    m.oversampling = std::max (1, m.oversampling);
    if (m == model)
        return;
    model = m;
    if (! hz.empty())
        setGrid (static_cast<int> (hz.size()), hz.front(), hz.back());
}

void ResponseCurves::bandResponse (const filters::BandShape& shape, const float* frequencies, int count, float* outDb) const
{
    const bool cut = shape.shape == Shape::LowCut || shape.shape == Shape::HighCut;
    if (model.firBrickwall && cut && shape.slope == Slope::Brickwall
        && shape.frequencyHz >= phase::minimumFirBrickwallHz (model.quality, fs))
    {
        // Natural Phase brickwall: the linear-phase FIR of the corrector.
        const auto taps = phase::brickwallTaps (shape.shape == Shape::LowCut, shape.frequencyHz, model.quality, fs);
        for (int i = 0; i < count; ++i)
        {
            std::complex<double> sum (0.0, 0.0);
            const double w = kTwoPi * frequencies[i] / fs;
            for (std::size_t n = 0; n < taps.size(); ++n)
                sum += taps[n] * std::polar (1.0, -w * static_cast<double> (n));
            outDb[i] = toDb (sum);
        }
        return;
    }

    const double rate = fs * model.oversampling;
    const auto design = filters::designBand (shape, rate);
    for (int i = 0; i < count; ++i)
        outDb[i] = toDb (model.analog ? design.analogResponse (frequencies[i]) : design.digitalResponse (frequencies[i], rate));
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
    computeDynamic (band);
    return true;
}

bool ResponseCurves::setDynamicGain (int band, float gainDb)
{
    constexpr float threshold = 0.05f;
    auto& current = dynamicGains[static_cast<std::size_t> (band)];
    const bool wasShown = std::abs (current) >= threshold;
    const bool show = std::abs (gainDb) >= threshold;
    if (! show && ! wasShown)
        return false;
    if (show && wasShown && std::abs (gainDb - current) < threshold)
        return false;

    current = show ? gainDb : 0.0f;
    computeDynamic (band);
    return true;
}

void ResponseCurves::setSpectralGains (int band, const float* gridHz, const float* gridDb, int numPoints)
{
    auto& curve = dynamicCurves[static_cast<std::size_t> (band)];
    const auto& shape = shapes[static_cast<std::size_t> (band)];
    dynamicGains[static_cast<std::size_t> (band)] = 0.0f;

    float largest = 0.0f;
    for (int i = 0; i < numPoints; ++i)
        largest = std::max (largest, std::abs (gridDb[i]));
    if (! shape.has_value() || numPoints < 2 || largest < 0.05f)
    {
        curve.clear();
        return;
    }

    const auto& base = curves[static_cast<std::size_t> (band)];
    curve.assign (hz.size(), 0.0f);
    int g = 0;
    for (std::size_t j = 0; j < hz.size(); ++j)
    {
        const float f = hz[j];
        while (g + 2 < numPoints && gridHz[g + 1] <= f)
            ++g;
        float change = 0.0f;
        if (f <= gridHz[0])
            change = gridDb[0];
        else if (f >= gridHz[numPoints - 1])
            change = gridDb[numPoints - 1];
        else
        {
            const float t = (f - gridHz[g]) / (gridHz[g + 1] - gridHz[g]);
            change = gridDb[g] + (gridDb[g + 1] - gridDb[g]) * t;
        }
        curve[j] = std::max (kFloorDb, base[j] + change);
    }
}

void ResponseCurves::computeDynamic (int band)
{
    auto& curve = dynamicCurves[static_cast<std::size_t> (band)];
    const auto& shape = shapes[static_cast<std::size_t> (band)];
    const float gain = dynamicGains[static_cast<std::size_t> (band)];
    if (! shape.has_value() || std::abs (gain) < 0.05f)
    {
        curve.clear();
        return;
    }
    auto s = *shape;
    s.gainDb += gain;
    curve.assign (hz.size(), 0.0f);
    compute (s, curve);
}

void ResponseCurves::updateComposite()
{
    composite.assign (hz.size(), 0.0f);
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! shapes[static_cast<std::size_t> (b)].has_value())
            continue;
        const auto& curve = hasDynamic (b) ? dynamicCurves[static_cast<std::size_t> (b)] : curves[static_cast<std::size_t> (b)];
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
    const auto f = static_cast<float> (std::min (static_cast<double> (frequencyHz), 0.4999 * fs));
    float db = 0.0f;
    bandResponse (shape, &f, 1, &db);
    return db;
}

void ResponseCurves::compute (const filters::BandShape& shape, std::vector<float>& out) const
{
    std::fill (out.begin(), out.end(), 0.0f);
    if (filters::isIdentity (shape))
        return;

    bandResponse (shape, hz.data(), validPoints, out.data());

    // Above Nyquist the digital filter has no response; hold the last value so curves end flat.
    const float last = validPoints > 0 ? out[static_cast<std::size_t> (validPoints - 1)] : 0.0f;
    for (std::size_t i = static_cast<std::size_t> (validPoints); i < out.size(); ++i)
        out[i] = last;
}
} // namespace ssc::ui
