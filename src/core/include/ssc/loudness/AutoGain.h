#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "ssc/fft/RealFft.h"
#include "ssc/loudness/Loudness.h"

namespace ssc::loudness
{
/** Time constant of the loudness averaging per Auto Gain speed (research §7c, AG-4). */
double autoGainSeconds (AutoGainSpeed speed) noexcept;

/**
    Global Auto Gain (AG-1, AG-3…AG-5): matches the loudness of the processed signal to the input.

    Feed-forward: the loudness of the latency-aligned input and of the processed signal before
    Auto Gain is measured in 1-ERB bands every hop (FFT ≈ 4096 at 48 kHz, Hann), averaged with the
    time constant of the selected speed, and the difference is applied as a smoothed gain to the
    processed channels. Stable compares the long-term input spectrum with the same spectrum
    shaped by the current EQ curve (setStaticCurve), so it follows settings within a second and
    never pumps. Silence below −70 dBFS freezes the measurement; the correction is limited to
    ±24 dB and changes by at most 0.5 dB/ms.
*/
class AutoGain
{
public:
    struct Settings
    {
        bool enabled = false;
        AutoGainWeighting weighting = AutoGainWeighting::Fletcher;
        AutoGainSpeed speed = AutoGainSpeed::Stable;
    };

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    /**
        Measures and corrects. input: latency-aligned dry signal; output: processed signal.
        channelWeights: BS.1770 weights of the channels to measure (0 = never, e.g. LFE);
        covered: channels processed by at least one band — only those are compared and corrected
        (AG-1, CH-6). The long-term input spectrum of every measured channel is kept up to date,
        so a band switched on later is compensated at once (Stable).
    */
    void process (const double* const* input, double* const* output, int numChannels, int numSamples, const Settings& settings,
                  const std::array<float, kMaxChannels>& channelWeights, const std::array<bool, kMaxChannels>& covered) noexcept;

    /** EQ magnitude (dB) of a channel at the centre of each ERB band, for the Stable speed. */
    void setStaticCurve (int channel, const double* gainsDb) noexcept;

    /** ERB bands of the main analysis (the bands of setStaticCurve). */
    const ErbBands& getBands() const noexcept { return analyses[0].bands; }

    /** Correction currently applied (dB); any thread. */
    float getCorrectionDb() const noexcept { return correctionDb.load (std::memory_order_relaxed); }

    static constexpr double kMaxCorrectionDb = 24.0;
    static constexpr double kGateDbFs = -70.0;

private:
    void analyseFrame (const Settings& settings, const std::array<float, kMaxChannels>& channelWeights,
                       const std::array<bool, kMaxChannels>& covered) noexcept;

    /** One analysis resolution: 4096 at 48 kHz, and 1024 for Very Fast (a short window keeps its 50 ms). */
    struct Analysis
    {
        int fftSize = 4096;
        int hop = 1024;
        RealFft fft;
        std::vector<float> window;
        double windowPower = 1.0;
        ErbBands bands;
    };

    const Analysis& analysisFor (AutoGainSpeed speed) const noexcept { return analyses[speed == AutoGainSpeed::VeryFast ? 1 : 0]; }

    double fs = 48000.0;
    int channels = 0;
    std::array<Analysis, 2> analyses;
    std::vector<float> frame, spectrum;
    int ringSize = 4096;

    std::vector<std::vector<double>> inputRing, outputRing;
    int ringPos = 0;
    int hopCounter = 0;

    std::vector<double> frameIn, frameOut, averageIn, averageOut, estimate;
    std::vector<std::vector<double>> frameInPerChannel; ///< band powers of the current frame per channel
    std::vector<std::vector<double>> longTermIn;        ///< long-term input spectrum per channel (Stable)
    std::vector<std::vector<double>> staticCurve;       ///< linear power gain per channel and band
    bool hasAverage = false;                            ///< long-term spectrum initialised (Stable)
    bool hasEstimate = false;                           ///< dB-domain smoothing initialised (other speeds)
    double smoothedDb = 0.0;
    bool wasEnabled = false;
    AutoGainSpeed lastSpeed = AutoGainSpeed::Stable;

    double targetDb = 0.0;
    double currentDb = 0.0;
    std::atomic<float> correctionDb { 0.0f };
};
} // namespace ssc::loudness
