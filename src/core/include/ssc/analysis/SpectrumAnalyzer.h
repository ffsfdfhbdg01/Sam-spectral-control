#pragma once

#include <vector>

#include "ssc/fft/RealFft.h"

namespace ssc::analysis
{
struct SpectrumAnalyzerConfig
{
    double sampleRate = 48000.0;
    int fftSize = 4096;         ///< power of two
    int hopSize = 1024;         ///< samples between frames
    double smoothingMs = 300.0; ///< exponential averaging time constant (power domain); 0 = off
    int numDisplayPoints = 512; ///< log-spaced output points
    double minHz = 10.0;
    double maxHz = 30000.0;       ///< clamped to Nyquist
    double tiltDbPerOctave = 0.0; ///< display tilt around 1 kHz
};

/**
    Windowed FFT spectrum analyzer (Blackman-Harris, overlapping frames) with exponential
    averaging and mapping to a logarithmic display grid.

    Levels are sine-calibrated: a full-scale sine reads 0 dB. On the display grid, every point
    shows the mean power of the bins it covers; prominent tonal peaks are refined with parabolic
    interpolation, so a pure tone shows its true level even between bins.

    prepare() allocates; push() is allocation-free.
*/
class SpectrumAnalyzer
{
public:
    void prepare (const SpectrumAnalyzerConfig& config);
    void reset() noexcept;

    /** Feeds samples; returns the number of new frames computed (0: the display did not change). */
    int push (const float* samples, int numSamples) noexcept;

    const SpectrumAnalyzerConfig& getConfig() const noexcept { return cfg; }
    const std::vector<float>& displayFrequencies() const noexcept { return displayHz; }
    const std::vector<float>& displayDb() const noexcept { return displayLevelDb; }

    /** Levels of the newest frame alone (no averaging, no tonal refinement) on the display grid, with the tilt. */
    const std::vector<float>& displayInstantDb() const noexcept { return displayInstantLevelDb; }

    /** Smoothed, sine-calibrated power per FFT bin (amplitude squared), bins 0 … N/2. */
    const std::vector<float>& binPower() const noexcept { return smoothedPower; }

    double binFrequency (int bin) const noexcept { return bin * cfg.sampleRate / cfg.fftSize; }

private:
    void computeFrame() noexcept;
    void updateDisplay() noexcept;
    void mapToDisplay (const std::vector<float>& power, std::vector<float>& outDb) const noexcept;

    SpectrumAnalyzerConfig cfg;
    RealFft fft;
    std::vector<float> window, input, frame, spectrum, smoothedPower, framePower;
    std::vector<float> displayHz, displayLevelDb, displayInstantLevelDb, displayTilt;
    std::vector<int> displayBinLo, displayBinHi;
    double smoothingAlpha = 0.0;
    float amplitudeScale = 1.0f;
    int writeIndex = 0;
    int samplesSinceFrame = 0;
    int samplesSeen = 0;
    bool hasFrame = false;
};
} // namespace ssc::analysis
