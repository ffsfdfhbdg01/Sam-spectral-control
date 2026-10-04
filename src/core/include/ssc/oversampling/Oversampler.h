#pragma once

#include <memory>
#include <vector>

#include "ssc/engine/Types.h"

/**
    Oversampling of the band chain (research §10, ADR-0008, OS-1…OS-3).

    Cascaded 2× stages, each a half-band filter:
    - PolyphaseIir: two branches of first-order allpass sections (elliptic half-band design after
      Valenzuela & Constantinides, the structure popularised by HIIR). Very cheap, low latency,
      phase not linear near the band edge — used with Minimum Phase.
    - LinearPhaseFir: Kaiser-windowed half-band FIR in polyphase form — linear phase and an exact
      integer latency; used with Natural Phase and Analog Feel.

    Each later stage only has to protect the passband of the base rate, so its transition band is
    wider and the filter much shorter. Implemented without JUCE so the DSP core stays
    framework-free (ADR-0008 named juce::dsp::Oversampling as the starting point; the
    specification is the same).
*/
namespace ssc::oversampling
{
enum class FilterType
{
    PolyphaseIir,
    LinearPhaseFir
};

/** Specification of the anti-imaging / anti-aliasing filters per Quality (research §10c, OS-2). */
struct FilterSpec
{
    double passband = 0.432;     ///< passband edge as a fraction of the base sample rate
    double attenuationDb = 90.0; ///< stopband attenuation (images and aliases of the passband)
    double rippleDb = 0.05;      ///< allowed passband deviation of the round trip
};

FilterSpec filterSpecFor (Quality quality) noexcept;

/** Allpass coefficients of a polyphase half-band IIR (branch 0: even indices, branch 1: odd).
    transitionWidth: width of the transition band as a fraction of the (high) sample rate. */
std::vector<double> designPolyphaseHalfband (double attenuationDb, double transitionWidth);

/** Taps of a half-band FIR (length 4M − 1, centre 0.5, every second tap zero, unity DC gain). */
std::vector<double> designHalfbandFir (double attenuationDb, double transitionWidth);

class Stage;

class Oversampler
{
public:
    Oversampler();
    ~Oversampler();
    Oversampler (Oversampler&&) noexcept;
    Oversampler& operator= (Oversampler&&) noexcept;

    /** factor: 1, 2, 4 or 8 (other values are rounded down). Allocates. */
    void prepare (int numChannels, int maxBlockSize, int factor, FilterType type, Quality quality);
    void reset() noexcept;

    int getFactor() const noexcept { return factor; }
    FilterType getFilterType() const noexcept { return filterType; }

    /** Latency of up + down in base-rate samples. Always an integer: the fractional part of the
        filter delay is completed by a delay at the highest rate (see getFilterDelay). */
    int getLatencySamples() const noexcept { return latency; }

    /** Low-frequency group delay of the filters alone (base-rate samples, fractional for the IIR). */
    double getFilterDelay() const noexcept { return filterDelay; }

    /** in: numSamples (≤ maxBlockSize) at the base rate; out: factor × numSamples. */
    void upsample (int channel, const double* in, double* out, int numSamples) noexcept;

    /** in: factor × numSamples at the high rate (used as scratch, so its contents are lost);
        out: numSamples at the base rate. */
    void downsample (int channel, double* in, double* out, int numSamples) noexcept;

private:
    int factor = 1;
    int channels = 0;
    int maxBlock = 0;
    FilterType filterType = FilterType::PolyphaseIir;
    int latency = 0;
    double filterDelay = 0.0;
    std::vector<std::unique_ptr<Stage>> stages;
    std::vector<double> work;

    // Delay at the highest rate completing the round trip to a whole number of base-rate samples.
    int padding = 0;
    std::vector<std::vector<double>> paddingLines;
    std::vector<int> paddingPos;
};
} // namespace ssc::oversampling
