#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

#include "ssc/engine/BandProcessor.h"
#include "ssc/fft/RealFft.h"

namespace ssc::phase
{
/** Latency D of the corrector: 16 / 32 / 64 / 128 samples at 48 kHz per Quality, scaled with fs (ADR-0005). */
int correctorDelayFor (Quality quality, double sampleRate) noexcept;

/**
    Lowest corner frequency at which a Brickwall cut is realised by the corrector as a linear-phase
    FIR: there its transition (≈ 5·fs/(2D + 1) with the brickwall window) is narrower than that of
    the 96 dB/oct IIR. Below it the band stays a phase-corrected minimum-phase IIR.
*/
double minimumFirBrickwallHz (Quality quality, double sampleRate) noexcept;

/** Taps (2D + 1, centred on D) of the linear-phase brickwall cut the corrector realises; for displays. */
std::vector<double> brickwallTaps (bool lowCut, double cornerHz, Quality quality, double sampleRate);

/** What the corrector needs to know about one band. */
struct CorrectorBand
{
    bool active = false;      ///< the band is heard (IIR chain, or a Natural Phase brickwall)
    bool brickwall = false;   ///< linear-phase brickwall cut realised by the corrector instead of the IIR
    filters::BandShape shape; ///< as heard: gain including the current dynamic change
    engine::LaneLayout lanes;
};

using CorrectorBands = std::array<CorrectorBand, kNumBands>;

/**
    Natural Phase (research §8c, ADR-0005, PH-1): one short FIR per channel after the band chain
    that turns the response of the matched IIR bands into that of their analog prototypes —
    phase and the small remaining magnitude error (BAND-2) — plus a pure delay D (the latency):

        C(ω) = B(ω) · T(ω)/A(ω) · e^{−jωD}

    T = product of the analog prototypes of the bands acting on the signal, A = their digital
    responses at the processing rate, B = magnitude of Natural Phase brickwall cuts (linear phase).
    Where a band is below −80 dB only its phase is corrected; the magnitude correction is limited
    to ±12 dB. The correction fades out between 0.95·Nyquist and Nyquist.
    C is sampled on a grid of ≥ 8·(2D + 1) points, transformed and windowed (Kaiser) to 2D + 1 taps
    centred on D, so any pre-ring is limited to D.

    Basis per channel pair: per channel while the bands on the pair are All/Left/Right; Mid/Side as
    soon as a band works on Mid or Side — then a band acting on only one channel of the pair is not
    corrected (no cross terms, documented approximation). Both are realised as a 2×2 FIR on the
    channel histories (y_L = P_L∗x_L + Q_L∗x_R), so a change of basis is a plain crossfade.

    Channels without corrected bands are delayed exactly (bit-exact). Per-band responses are cached
    and recomputed only when a band changes; new filters are crossfaded in. update() is meant to be
    called at fixed sample positions (getUpdateInterval()) so that the output does not depend on the
    host block size. prepare() allocates; everything else is real-time safe.

    Minimum-phase mode (Minimum Phase at Quality High and Max, PH-1): only the magnitude |T/A| is
    corrected, by a minimum-phase FIR built from it through the real cepstrum (D + 1 taps of
    48 kHz at any rate), with no latency. The bands then have the magnitude of their analog
    prototypes and stay minimum phase. A channel whose correction stays below 0.02 dB up to
    20 kHz (high sample rates) and channels without corrected bands pass bit-exact.
*/
class PhaseCorrector
{
public:
    void prepare (double sampleRate, int numChannels, int maxBlockSize);
    void reset() noexcept;

    /** Selects D for a Quality level, and the mode: Natural Phase (linear-phase centre D) or
        minimum phase (no latency); returns true (and resets the filters) if either changed. */
    bool setQuality (Quality quality, bool minimumPhase = false) noexcept;
    int getLatencySamples() const noexcept { return delay; }
    bool isMinimumPhase() const noexcept { return minimumPhase; }

    /** Interval (samples) at which the engine updates the correction (≈ 21 ms). */
    int getUpdateInterval() const noexcept { return updateInterval; }

    /** Recomputes the filters from the bands; processRate is the rate of the IIR designs (fs × oversampling). */
    void update (const CorrectorBands& bands, const engine::ChannelTopology& topology, double processRate) noexcept;

    /** Filters numSamples (≤ maxBlockSize) of every channel in place. */
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    /** True while any channel is filtered (not just delayed). */
    bool isCorrecting() const noexcept;

private:
    struct Channel
    {
        std::vector<double> history;     ///< (maxLength − 1) previous samples, then the current block
        std::vector<double> self, cross; ///< reversed taps: from this channel and from its partner
        std::vector<double> oldSelf, oldCross;
        bool selfDelay = true, crossZero = true;
        bool oldSelfDelay = true, oldCrossZero = true;
        int fade = 0;
        int partner = -1;
        // What the current taps were built from.
        std::uint32_t builtA = 0, builtB = 0;
        std::uint64_t builtVersion = 0;
        bool builtMs = false;
    };

    void computeBandFactor (int band, const CorrectorBand& settings) noexcept;
    /** Designs the reversed taps of C for a set of bands; returns false for a pure delay (mask 0). */
    bool designTaps (std::uint32_t mask, std::vector<double>& reversed) noexcept;
    /** The minimum-phase FIR of the magnitude on the spectrum grid (log magnitudes in `spectrum`). */
    void minimumPhaseTaps (std::vector<double>& reversed) noexcept;
    /** Reversed taps of a unit impulse at the corrector's delay (a pure delay as a filter). */
    void impulseTaps (std::vector<double>& reversed) const noexcept;
    std::uint64_t versionOf (std::uint32_t mask) const noexcept;
    void install (Channel& ch, bool selfDelay, bool crossZero) noexcept;

    double fs = 48000.0;
    int numChannels = 0;
    int maxBlock = 0;
    int maxDelay = 0, maxLength = 1;
    int delay = 0, length = 1;
    bool minimumPhase = false;
    int gridSize = 512, numPoints = 257;
    int updateInterval = 1024;
    int fadeLength = 256;
    Quality quality = Quality::Normal;
    double rate = 0.0; ///< processing rate the band factors were computed for

    std::array<RealFft, 4> ffts; ///< per Quality (grid sizes differ)
    std::array<int, 4> gridSizes {};
    std::vector<double> window, brickwallWindow, minimumPhaseWindow;
    std::vector<std::complex<double>> delayPhase; ///< e^{−jω_k·D} on the current grid
    std::vector<std::complex<double>> zInverse;   ///< e^{−j2πf_k/processRate}
    std::vector<float> spectrum, time;
    std::vector<double> tapsA, tapsB;

    std::vector<std::vector<std::complex<double>>> factors; ///< [band][grid point]
    std::array<bool, kNumBands> factorValid {};
    std::array<bool, kNumBands> factorBrickwall {};
    std::array<filters::BandShape, kNumBands> factorShape {};
    std::array<std::uint64_t, kNumBands> versions {};

    std::vector<Channel> state;
};
} // namespace ssc::phase
