#pragma once

#include <complex>
#include <vector>

#include "ssc/engine/DistortionCapture.h"
#include "ssc/fft/RealFft.h"
#include "ssc/oversampling/Oversampler.h"

namespace ssc::analysis
{
/** Distortion of one band nonlinearity: mean powers per sample of the recorded lane. */
struct ShaperDistortion
{
    double products = 0.0; ///< all it adds in the output band (harmonics, IMD, aliases); its gain change excluded
    double aliasing = 0.0; ///< the part an alias-free nonlinearity would not add, decimation leakage included
};

/**
    Shadow measurement of a band nonlinearity (research §13c, MTR-2), from the input x it saw at the
    processing rate fp (the base rate × oversampling).

    Products (THD+N): the actual products as the audio path makes them — the ADAA of N between
    consecutive inputs — minus their linear part β·(x[n] + x[n−1])/2, a least-squares fit (the gain
    change of a memoryless curve, Bussgang, is not distortion); their power in the output band (up
    to half the base rate), harmonics, IMD and aliases alike.

    Aliasing: the ADAA output is a polynomial in two consecutive inputs, y[n] = A(x[n], x[n−1]) with
    A(u, w) = (∫N(u) − ∫N(w)) / (u − w). The shadow evaluates the same A on the band-limited input
    and its copy one input period earlier, ŷ(t) = A(x(t), x(t − 1/fp)), at 4× the base rate (2× fp
    with oversampling): the input interpolated by a linear-phase FIR that keeps the original
    samples, so ŷ(n/fp) = y[n] exactly. ŷ has only the true combination frequencies of the input
    (no images), up to 2.5× the base rate for a quintic curve; sampling at fp folds what lies above
    fp/2 — that is the aliasing, the new frequencies a tone measurement finds between its harmonics.

    With oversampling, the actual between half the base rate and half fp passes the decimation
    filter attenuated (Oversampler::decimationGain) and folds into the band: it counts in both.
    The DC blocker and the low-pass of the products weight the spectra at the frequency where they
    act (the folded one). Windows: Hann over BandCapture::windowFor() samples of fp.
    Runs on a background thread; prepare() allocates, analyse() does not.
*/
class DistortionMeter
{
public:
    static constexpr int kStart = engine::BandCapture::kMargin; ///< first analysed sample of a capture

    void prepare (double baseRate, int osFactor, oversampling::FilterType type, Quality quality);
    bool isPreparedFor (double baseRate, int osFactor, oversampling::FilterType type, Quality quality) const noexcept;

    /** x: engine::BandCapture::lengthFor (osFactor) inputs of the nonlinearity, oldest first. */
    ShaperDistortion analyse (const float* x, filters::ShaperKind kind) noexcept;

    /** Interpolation factor of the shadow (its rate is fp × this). */
    int getShadowFactor() const noexcept { return shadowFactor; }
    /** Delay of the shadow's interpolator in samples of the shadow rate: the analysed part of a
        capture must start after it has settled and end before the capture does (≤ kStart × factor). */
    int getShadowDelay() const noexcept { return shadowDelay; }

private:
    using Spectrum = std::vector<std::complex<double>>;

    /** Removes the window-weighted mean, applies the window and transforms (bins 0 … size/2). */
    void windowedSpectrum (RealFft& fft, const double* signal, const std::vector<double>& taper, Spectrum& out) noexcept;

    double fb = 0.0, fp = 0.0;
    int osFactor = 0;
    oversampling::FilterType osType = oversampling::FilterType::PolyphaseIir;
    Quality osQuality = Quality::Normal;
    int window = 1024; ///< analysed samples (BandCapture::windowFor())
    int shadowFactor = 2;
    int shadowDelay = 0; ///< of the interpolator, in samples of the shadow rate
    int bandBins = 0;    ///< bins up to half the base rate

    oversampling::Oversampler interpolator;
    RealFft shadowFft, actualFft;
    std::vector<double> actualWindow, shadowWindow, decimation;
    double windowNorm = 0.0;

    // Scratch.
    std::vector<double> xs, actual, actualLinear, upsampled, shadow;
    std::vector<float> fftIn, fftOut;
    Spectrum actualSpec, actualLinearSpec, shadowSpec;
};
} // namespace ssc::analysis
