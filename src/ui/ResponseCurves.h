#pragma once

#include <array>
#include <optional>
#include <vector>

#include "ssc/engine/Types.h"
#include "ssc/filters/BandDesign.h"

namespace ssc::ui
{
/**
    Magnitude responses (dB) of the bands on the display's frequency grid, cached per band and
    recomputed only when a band's shape or the sample rate changes. The curves are those of the
    digital filters actually used (matched design), up to Nyquist.
*/
class ResponseCurves
{
public:
    static constexpr float kFloorDb = -150.0f;

    /** Sets the log-spaced frequency grid; invalidates every curve. */
    void setGrid (int numPoints, float minHz, float maxHz);
    void setSampleRate (double sampleRate);

    /** Updates one band (nullopt = band not drawn); returns true if its curve changed. */
    bool updateBand (int band, const std::optional<filters::BandShape>& shape);

    /**
        Sets the current dynamic gain change of a band (DYN-11). Changes below 0.05 dB are ignored;
        returns true when the band's dynamic curve changed.
    */
    bool setDynamicGain (int band, float gainDb);

    /**
        Sets the current gain change of a Spectral band as a function of frequency (sorted points);
        the drawn dynamic curve is the static curve plus this change.
    */
    void setSpectralGains (int band, const float* hz, const float* db, int numPoints);

    /** Recomputes the composite (sum in dB of every drawn band, dynamic curves where present). */
    void updateComposite();

    int numPoints() const noexcept { return static_cast<int> (hz.size()); }
    float frequency (int point) const noexcept { return hz[static_cast<std::size_t> (point)]; }
    /** Number of grid points below Nyquist. */
    int numValidPoints() const noexcept { return validPoints; }

    bool hasBand (int band) const noexcept { return shapes[static_cast<std::size_t> (band)].has_value(); }
    const std::vector<float>& bandDb (int band) const noexcept { return curves[static_cast<std::size_t> (band)]; }
    bool hasDynamic (int band) const noexcept { return ! dynamicCurves[static_cast<std::size_t> (band)].empty(); }
    const std::vector<float>& dynamicDb (int band) const noexcept { return dynamicCurves[static_cast<std::size_t> (band)]; }
    const std::vector<float>& compositeDb() const noexcept { return composite; }

    /** Response of an arbitrary shape on the grid (ghost previews). */
    std::vector<float> responseOf (const filters::BandShape& shape) const;

    /** Response of a shape at one frequency, in dB. */
    float responseAt (const filters::BandShape& shape, float frequencyHz) const;

    double getSampleRate() const noexcept { return fs; }

private:
    void compute (const filters::BandShape& shape, std::vector<float>& out) const;
    void computeDynamic (int band);

    std::vector<float> hz;
    int validPoints = 0;
    double fs = 48000.0;
    std::array<std::optional<filters::BandShape>, kNumBands> shapes {};
    std::array<std::vector<float>, kNumBands> curves {};
    std::array<float, kNumBands> dynamicGains {};
    std::array<std::vector<float>, kNumBands> dynamicCurves {};
    std::vector<float> composite;
};
} // namespace ssc::ui
