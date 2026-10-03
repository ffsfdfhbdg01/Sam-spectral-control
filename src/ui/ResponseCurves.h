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

    /** Recomputes the composite (sum in dB of every drawn band); call after updateBand(). */
    void updateComposite();

    int numPoints() const noexcept { return static_cast<int> (hz.size()); }
    float frequency (int point) const noexcept { return hz[static_cast<std::size_t> (point)]; }
    /** Number of grid points below Nyquist. */
    int numValidPoints() const noexcept { return validPoints; }

    bool hasBand (int band) const noexcept { return shapes[static_cast<std::size_t> (band)].has_value(); }
    const std::vector<float>& bandDb (int band) const noexcept { return curves[static_cast<std::size_t> (band)]; }
    const std::vector<float>& compositeDb() const noexcept { return composite; }

    /** Response of an arbitrary shape on the grid (ghost previews). */
    std::vector<float> responseOf (const filters::BandShape& shape) const;

    /** Response of a shape at one frequency, in dB. */
    float responseAt (const filters::BandShape& shape, float frequencyHz) const;

    double getSampleRate() const noexcept { return fs; }

private:
    void compute (const filters::BandShape& shape, std::vector<float>& out) const;

    std::vector<float> hz;
    int validPoints = 0;
    double fs = 48000.0;
    std::array<std::optional<filters::BandShape>, kNumBands> shapes {};
    std::array<std::vector<float>, kNumBands> curves {};
    std::vector<float> composite;
};
} // namespace ssc::ui
