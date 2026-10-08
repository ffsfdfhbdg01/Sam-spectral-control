#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "ssc/engine/Engine.h"
#include "ssc/fft/RealFft.h"

namespace ssc::analysis
{
/** Readouts of one band as heard (research §12c, PR-1). Times in ms, phases in degrees. */
struct BandReadout
{
    bool valid = false;
    float meanPhaseDeg = 0.0f;    ///< weighted mean |excess phase| where the band acts (|gain| ≥ 0.5 dB)
    float groupDelayMs = 0.0f;    ///< excess group delay at fc (the plugin latency removed)
    float preRingMs = 0.0f;       ///< energy above −60 dB of the peak before it
    float postRingMs = 0.0f;      ///< … and after it
    float rangePreRingMs = -1.0f; ///< Dynamic bands: the same at the full range (−1: not a Dynamic band)
    float rangePostRingMs = -1.0f;
    float stftPreEchoMs = -1.0f; ///< Spectral bands: half the STFT frame (−1: not a Spectral band)
};

/**
    Phase and group delay of the whole EQ as heard (PR-4), on a log frequency grid from 10 Hz: the
    left channel for a centred source (L = R), so the bands on All, Left and Mid act, Dynamic bands
    at their current gain; the engine latency removed. Spectral bands are zero-phase (STFT mask)
    and left out.
*/
struct ChainResponse
{
    static constexpr int kMaxPoints = 256;
    bool valid = false;
    int numPoints = 0;
    std::array<float, kMaxPoints> hz {};
    std::array<float, kMaxPoints> phaseDeg {};     ///< excess phase, wrapped to (−180°, 180°]
    std::array<float, kMaxPoints> groupDelayMs {}; ///< excess group delay
    std::array<bool, kMaxPoints> defined {};       ///< false where |H| < −60 dB (deep cuts, notches)
};

/**
    Measures the readouts of a band with the same DSP code as the audio path (research §12c): the
    band alone runs through an Engine with the plugin's phase mode, Quality, oversampling and
    Analog Feel model, fed with a small impulse after a silent pre-roll (smoothing settled; small
    so that a Character's nonlinearity stays negligible). From the impulse response h:
    - group delay τ(ω) = Re{DFT(n·h) / DFT(h)}, minus the engine latency, at fc,
    - excess phase: the phase with the latency removed, unwrapped from the frequency where the
      band does least; weighted by |gain in dB| where |gain| ≥ 0.5 dB,
    - pre-ring: from the first time the envelope (peaks of |h|, interpolated in dB) is within
      60 dB of the peak to the system's zero time (the engine latency, where an unfiltered impulse
      would appear) — 0 for causal, Minimum Phase bands by definition; post-ring: from the peak to
      the last time; the response is followed until it stays 90 dB down (≤ 5 s).
    Dynamic bands: at the current gain and at the full range. Spectral bands: the STFT mask is
    zero-phase; the pre-echo is reported as half the frame.

    Runs on a background thread: prepare() and compute() allocate.
*/
class BandReadouts
{
public:
    void prepare (double sampleRate);

    /** Polled between blocks of a measurement: true stops it (compute() then returns an invalid
        readout), so a background thread can be stopped promptly. */
    void setAbortCheck (std::function<bool()> check) { abortCheck = std::move (check); }

    /** dynamicGainDb: the band's current gain change (Dynamic bands). */
    BandReadout compute (const engine::BandSettings& band, const engine::GlobalParams& params, double dynamicGainDb);

    /** Impulse response of the last measurement (for tests). */
    const std::vector<double>& lastImpulseResponse() const noexcept { return response; }
    int lastLatencySamples() const noexcept { return latency; }

    struct Measurement
    {
        float meanPhaseDeg = 0.0f, groupDelayMs = 0.0f, preRingMs = 0.0f, postRingMs = 0.0f;
    };

    /** Measures one static band shape (the work of compute()). */
    Measurement measure (const engine::BandSettings& band, const engine::GlobalParams& params);

    /** The response of the whole EQ (PR-4); dynamicGainsDb: the current gain change of each band. */
    ChainResponse computeChain (const engine::BandSettingsArray& bands, const engine::GlobalParams& params,
                                const std::array<float, kNumBands>& dynamicGainsDb);

private:
    /** Impulse response of `bands` through `e` (every channel fed, the first recorded) into `response`. */
    void runImpulse (engine::Engine& e, int numChannels, const engine::BandSettingsArray& bands, const engine::GlobalParams& params);
    /** Spectra of `response` and of n·response (for the group delay), zero-padded to at least
        minimumSize points; returns the FFT size. */
    int responseSpectra (int minimumSize = 16384);

    bool aborted() const { return abortCheck && abortCheck(); }

    double fs = 48000.0;
    std::function<bool()> abortCheck;
    std::unique_ptr<engine::Engine> engine;
    std::unique_ptr<engine::Engine> chainEngine; ///< stereo, prepared on first use
    std::vector<double> response;
    RealFft fft;
    std::vector<float> buffer, spectrum, rampSpectrum;
    int latency = 0;
};

/**
    Ring times of an impulse response at −thresholdDb of its peak (samples; see BandReadouts):
    pre-ring up to min(peak, zeroIndex), post-ring from the peak.
*/
struct RingTimes
{
    double pre = 0.0, post = 0.0;
    std::size_t peak = 0;
};
RingTimes ringTimes (const std::vector<double>& h, double thresholdDb = 60.0, std::size_t zeroIndex = ~std::size_t { 0 });
} // namespace ssc::analysis
