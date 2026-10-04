#pragma once

#include <array>
#include <cstdint>

#include "ssc/engine/Types.h"
#include "ssc/filters/Character.h"
#include "ssc/oversampling/Oversampler.h"

namespace ssc::engine
{
/**
    What the distortion meters receive about one band per measuring period (research §13c,
    MTR-2): the input of its nonlinearity at the processing rate, for the shadow analysis, and
    the power of its fast gain modulation. Recorded on the audio thread, analysed elsewhere.
*/
struct BandCapture
{
    static constexpr int kMargin = 64;      ///< samples before and after the analysed part
    static constexpr int kMaxLength = 2176; ///< at the processing rate: 2048 analysed + margins

    /** Analysed samples: 1024 at 1×; 2048 with oversampling, which keeps the frequency resolution
        near half the base rate (the decimation filter's transition band) and the recording time. */
    static constexpr int windowFor (int osFactor) noexcept { return osFactor >= 2 ? 2048 : 1024; }
    static constexpr int lengthFor (int osFactor) noexcept { return windowFor (osFactor) + 2 * kMargin; }

    bool shaped = false; ///< the nonlinearity ran for the whole capture: x is valid
    filters::ShaperKind kind = filters::ShaperKind::None;
    double lanesScale = 1.0;            ///< products power reaching the output from all lanes / from the recorded lane
    double modulationPower = 0.0;       ///< Dynamic bands: mean power of the gain-modulation products in the output
    std::array<float, kMaxLength> x {}; ///< nonlinearity input of the recorded lane, oldest first (lengthFor (osFactor))
};

/** One measuring period of the engine, published to the meters. */
struct DistortionCapture
{
    double baseRate = 48000.0;
    int osFactor = 1;
    oversampling::FilterType osType = oversampling::FilterType::PolyphaseIir;
    Quality osQuality = Quality::Normal;
    double periodSeconds = 0.0;
    double outputPower = 0.0;  ///< mean over the period of Σ over the output channels of out²
    double productsGain = 1.0; ///< power gain from the band products to the output (Auto Gain, Mix, Output, Bypass)
    std::array<BandCapture, kNumBands> bands {};
    std::uint64_t serial = 0;
};

/**
    Audio-thread recorder of one band's nonlinearity (see BandProcessor::setRecorder()). The engine
    attaches it for the capture window at the end of every measuring period: the ring then holds
    the latest inputs of one lane, the energies of the products of every lane tell how much the
    other lanes add.
*/
struct BandRecorder
{
    static constexpr int kRingSize = 4096; ///< ≥ BandCapture::kMaxLength, a power of two
    static constexpr int kRingMask = kRingSize - 1;

    std::array<float, kRingSize> ring {};
    int position = 0; ///< next write (masked)
    int recorded = 0; ///< consecutive inputs recorded since the window started
    int lane = 0;     ///< the lane recorded
    filters::ShaperKind kind = filters::ShaperKind::None;
    std::array<double, kMaxChannels> laneEnergy {};        ///< Σ products² per lane over the window
    std::array<double, kMaxChannels> laneWeight {};        ///< output channels each lane reaches (1, or 2 for Mid and Side)
    double modulationEnergy = 0.0;                         ///< Σ band signal power × fast gain change², output weights included
    int modulationSamples = 0;                             ///< samples the modulation was measured over
    std::array<double, kMaxChannels> gainS1 {}, gainS2 {}; ///< high-pass states of the linear band gain per lane
    double modulationCutoff = -1.0, modulationG = 0.0;
    bool modulationPrimed = false;
    bool shapedThisBlock = false;

    /** Starts a capture window (the lane stays). */
    void startWindow() noexcept
    {
        recorded = 0;
        laneEnergy.fill (0.0);
        modulationEnergy = 0.0;
        modulationSamples = 0;
        modulationPrimed = shapedThisBlock = false;
    }

    void clear() noexcept
    {
        startWindow();
        position = lane = 0;
        kind = filters::ShaperKind::None;
    }
};
} // namespace ssc::engine
