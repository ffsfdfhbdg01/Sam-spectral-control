#pragma once

struct PFFFT_Setup;

namespace ssc
{
/**
    Real-input FFT (wrapper around pffft).

    Spectrum layout: N + 2 floats of interleaved complex values for bins k = 0 … N/2
    (re0, im0, re1, im1, …, reN/2, imN/2), with im0 = imN/2 = 0.
    forward() is unscaled; inverse() is scaled by 1/N, so inverse(forward(x)) == x.

    prepare() allocates; forward() and inverse() are real-time safe.
*/
class RealFft
{
public:
    RealFft() = default;
    explicit RealFft (int size) { prepare (size); }
    ~RealFft();

    RealFft (const RealFft&) = delete;
    RealFft& operator= (const RealFft&) = delete;
    RealFft (RealFft&& other) noexcept;
    RealFft& operator= (RealFft&& other) noexcept;

    /** size must be a power of two, at least 32. */
    void prepare (int size);

    int size() const noexcept { return n; }

    void forward (const float* time, float* spectrum) noexcept;
    void inverse (const float* spectrum, float* time) noexcept;

private:
    void release() noexcept;

    PFFFT_Setup* setup = nullptr;
    float* buffer = nullptr;
    float* work = nullptr;
    int n = 0;
};
} // namespace ssc
