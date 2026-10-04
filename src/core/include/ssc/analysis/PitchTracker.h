#pragma once

#include <vector>

#include "ssc/fft/RealFft.h"

namespace ssc::analysis
{
/**
    Fundamental frequency of the dominant periodic sound (YIN: A. de Cheveigné, H. Kawahara,
    "YIN, a fundamental frequency estimator for speech and music", JASA 111(4), 2002).

    Used by the resonance hints to tell note harmonics from resonances (research §11c). The
    difference function is computed through an FFT cross-correlation; the cumulative mean
    normalised difference picks the first dip below the threshold, refined by parabolic
    interpolation.

    prepare() allocates; analyse() is allocation-free.
*/
class PitchTracker
{
public:
    struct Result
    {
        double f0Hz = 0.0;         ///< 0: no pitch
        double aperiodicity = 1.0; ///< normalised difference at the period (0 = perfectly periodic)
        bool voiced() const noexcept { return f0Hz > 0.0 && aperiodicity < 0.2; }
    };

    /** window: the integration window W in samples; frames are 2·W samples long. */
    void prepare (double sampleRate, int window, double minHz = 50.0, double maxHz = 1000.0, double threshold = 0.15);

    int frameLength() const noexcept { return 2 * w; }

    /** Analyses one frame of frameLength() samples, oldest first. */
    Result analyse (const float* frame) noexcept;

private:
    RealFft fft;
    std::vector<float> a, b, specA, specB, product, correlation;
    std::vector<double> energy, difference;
    double fs = 48000.0, threshold = 0.15;
    int w = 1024, tauMin = 48, tauMax = 960, n = 4096;
};
} // namespace ssc::analysis
