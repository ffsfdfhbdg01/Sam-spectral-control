#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "ssc/engine/BandProcessor.h"
#include "ssc/fft/RealFft.h"
#include "ssc/util/TripleBuffer.h"

namespace ssc::spectral
{
/** ERB-number scale (Glasberg & Moore 1990): ERBs below a frequency. */
inline double erbNumber (double hz) noexcept
{
    return 21.4 * std::log10 (1.0 + 0.00437 * hz);
}

inline double erbToHz (double erb) noexcept
{
    return (std::pow (10.0, erb / 21.4) - 1.0) / 0.00437;
}

/** Frame size of the spectral engine (ADR-0006): 2048 / 4096 / 8192 / 8192 at 48 kHz, scaled with fs. */
int fftSizeFor (Quality quality, double sampleRate) noexcept;

/** Hop size: a quarter frame, an eighth in Max. */
int hopFor (Quality quality, int fftSize) noexcept;

/**
    Spectral dynamics engine for bands in Spectral mode (research §6c, ADR-0006, DYN-3, DYN-9, DYN-10).

    - STFT with √Hann analysis and synthesis windows and weighted overlap-add; latency = frame size.
    - Detection and gains live on a grid of 8 points per ERB: the level at each point is the energy
      within a band whose width follows Selectivity (2 ERB … 1/8 ERB), in dBFS (a sine of amplitude
      A reads 20·log10 A). Relative thresholds compare it with the mean level within ±2 ERB.
    - Each band weighs its gain by its own shape (1 at its centre or plateau), so it only acts in
      its region; per-point attack/release smoothing runs at the hop rate.
    - Gains of all Spectral bands add up (dB) into one mask per channel and per channel pair (Mid
      and Side), interpolated from the grid to the FFT bins.
    - Channels without spectral lanes bypass the STFT through an exact delay of the same length.

    prepare() allocates for the largest frame; process() is real-time safe.
*/
class SpectralEngine
{
public:
    static constexpr int kPointsPerErb = 8;
    static constexpr int kMaxGridPoints = 344; ///< 20 Hz … 20 kHz at 8 points per ERB, with margin

    /** Current gain change of every Spectral band on the ERB grid, for the display (DYN-11). */
    struct MaskView
    {
        int numPoints = 0;
        std::array<float, kMaxGridPoints> hz {};
        std::array<std::array<float, kMaxGridPoints>, kNumBands> bandDb {};
        std::array<bool, kNumBands> bandActive {};
    };

    /** Reader side (one thread, e.g. the UI): the newest mask view, or nullptr if nothing new. */
    const MaskView* pollMaskView() noexcept { return maskViews->update() ? &maskViews->read() : nullptr; }

    void prepare (double sampleRate, int numChannels, int maxBlockSize);
    void reset() noexcept;

    /** Selects the frame size for a Quality level; returns true (and resets) if it changed. */
    bool setQuality (Quality quality) noexcept;

    int getLatencySamples() const noexcept { return fftSize; }
    int getHopSize() const noexcept { return hop; }

    /** Processes the channels in place; everything is delayed by getLatencySamples(). */
    void process (double* const* channels, int numChannels, int numSamples, const engine::BandSettingsArray& bands,
                  const engine::ChannelTopology& topology) noexcept;

    /** Largest gain change of a band over its region (dB, signed); any thread (DYN-11). */
    float getBandGainDb (int band) const noexcept { return bandGains[static_cast<std::size_t> (band)].load (std::memory_order_relaxed); }

private:
    struct GridPoint
    {
        double erb = 0.0;
        double hz = 0.0;
    };

    struct BandState
    {
        filters::BandShape weightShape;
        bool weightsValid = false;
        engine::LaneLayout lanes;
        std::vector<float> weights;              ///< per grid point, 0 … 1
        std::vector<float> gains;                ///< per lane and grid point (dB), smoothed
        double weightedGain = 0.0, weight = 0.0; ///< energy-weighted mean of the change (AG-2)
        float makeup = 0.0f;                     ///< automatic make-up (dB) at the band's centre
    };

    void configureSize (int sizeIndex) noexcept;
    void runFrame (const engine::BandSettingsArray& bands, const engine::ChannelTopology& topology) noexcept;
    void updateWeights (BandState& state, const filters::BandShape& shape) noexcept;
    void laneLevels (const engine::Lane& lane, const engine::BandSettings& band, float* levelsOut, float* absoluteOut) noexcept;
    void applyMask (const float* maskDb, float* gainsOut) noexcept;

    double fs = 48000.0;
    int channels = 0;
    std::array<RealFft, 3> ffts;
    std::array<std::vector<float>, 3> windows;
    std::array<double, 3> windowPower {}; ///< Σ w² per size
    std::array<int, 3> sizes {};
    int sizeIndex = 1;
    int fftSize = 4096;
    int hop = 1024;
    Quality quality = Quality::Normal;

    std::vector<std::vector<double>> inputRing;
    std::vector<std::vector<float>> outputAccum;
    int ringPos = 0;
    int hopCounter = 0;
    std::vector<float> frame;
    std::vector<std::vector<float>> spectra; ///< per channel, interleaved complex, N + 2

    std::vector<GridPoint> grid;
    int numGrid = 0;
    std::vector<int> binGridIndex; ///< per bin: grid point below
    std::vector<float> binGridFrac;
    std::vector<double> powerPrefix;
    std::vector<double> levelPrefix;
    std::vector<float> laneLevel;    ///< [lane][grid]
    std::vector<float> laneAbsolute; ///< [lane][grid] absolute level, weights the make-up
    std::vector<float> linkLevel;    ///< [lane][grid]
    std::vector<float> maskScratch;  ///< linear gains per grid point
    std::vector<float> binGains, binGainsB;

    std::vector<float> channelMask;       ///< [channel][grid] dB
    std::vector<float> midMask, sideMask; ///< [pair][grid] dB
    std::array<bool, kMaxChannels> channelHasMask {}, pairHasMid {}, pairHasSide {}, channelSpectral {};
    std::array<float, kMaxChannels> stftAmount {};

    std::array<BandState, kNumBands> states;
    std::unique_ptr<TripleBuffer<MaskView>> maskViews = std::make_unique<TripleBuffer<MaskView>>();
    std::array<std::atomic<float>, kNumBands> bandGains {};
};
} // namespace ssc::spectral
