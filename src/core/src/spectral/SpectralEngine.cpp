#include "ssc/spectral/SpectralEngine.h"

#include <algorithm>
#include <functional>

#include "ssc/dynamics/Dynamics.h"
#include "ssc/util/Math.h"

namespace ssc::spectral
{
namespace
{
    constexpr double kGridLowHz = 20.0;
    constexpr double kGridHighHz = 20000.0;
    constexpr double kRelativeHalfWidthErb = 2.0;
    constexpr double kWeightReferenceDb = 6.0;
    constexpr float kSilenceDb = -200.0f;

    int baseSizeFor (double sampleRate) noexcept
    {
        // 2048 at 44.1/48 kHz, doubled for each doubling of the sample rate.
        return nextPowerOfTwo (static_cast<int> (std::lround (2048.0 * sampleRate / 48000.0 * 0.75)) + 1);
    }

    /** Selectivity 0 … 1 → detection width 2 ERB … 1/8 ERB (log-interpolated). */
    double detectionWidthErb (double selectivity) noexcept
    {
        return std::pow (2.0, 1.0 - 4.0 * std::clamp (selectivity, 0.0, 1.0));
    }
} // namespace

int fftSizeFor (Quality q, double sampleRate) noexcept
{
    const int base = baseSizeFor (sampleRate);
    switch (q)
    {
        case Quality::Eco: return base;
        case Quality::Normal: return base * 2;
        case Quality::High:
        case Quality::Max: return base * 4;
    }
    return base * 2;
}

int hopFor (Quality q, int size) noexcept
{
    return q == Quality::Max ? size / 8 : size / 4;
}

void SpectralEngine::prepare (double sampleRate, int numChannels, int /*maxBlockSize*/)
{
    fs = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);

    const int base = baseSizeFor (fs);
    for (int i = 0; i < 3; ++i)
    {
        const int size = base << i;
        sizes[static_cast<std::size_t> (i)] = size;
        ffts[static_cast<std::size_t> (i)].prepare (size);
        auto& w = windows[static_cast<std::size_t> (i)];
        w.resize (static_cast<std::size_t> (size));
        double power = 0.0;
        for (int n = 0; n < size; ++n)
        {
            const double hann = 0.5 - 0.5 * std::cos (kTwoPi * n / size);
            w[static_cast<std::size_t> (n)] = static_cast<float> (std::sqrt (hann));
            power += hann;
        }
        windowPower[static_cast<std::size_t> (i)] = power;
    }

    const int maxSize = sizes[2];
    const auto ch = static_cast<std::size_t> (channels);
    inputRing.assign (ch, std::vector<double> (static_cast<std::size_t> (maxSize), 0.0));
    outputAccum.assign (ch, std::vector<float> (static_cast<std::size_t> (maxSize), 0.0f));
    spectra.assign (ch, std::vector<float> (static_cast<std::size_t> (maxSize + 2), 0.0f));
    frame.assign (static_cast<std::size_t> (maxSize), 0.0f);
    powerPrefix.assign (static_cast<std::size_t> (maxSize / 2 + 2), 0.0);
    binGridIndex.assign (static_cast<std::size_t> (maxSize / 2 + 1), 0);
    binGridFrac.assign (static_cast<std::size_t> (maxSize / 2 + 1), 0.0f);
    binGains.assign (static_cast<std::size_t> (maxSize / 2 + 1), 1.0f);
    binGainsB.assign (static_cast<std::size_t> (maxSize / 2 + 1), 1.0f);

    // Grid: 8 points per ERB from 20 Hz to min (20 kHz, 0.45·fs).
    const double erbLow = erbNumber (kGridLowHz);
    const double erbHigh = erbNumber (std::min (kGridHighHz, 0.45 * fs));
    numGrid = std::min (kMaxGridPoints, static_cast<int> (std::floor ((erbHigh - erbLow) * kPointsPerErb)) + 1);
    grid.resize (static_cast<std::size_t> (numGrid));
    for (int i = 0; i < numGrid; ++i)
    {
        auto& g = grid[static_cast<std::size_t> (i)];
        g.erb = erbLow + static_cast<double> (i) / kPointsPerErb;
        g.hz = erbToHz (g.erb);
    }

    const auto gridSize = static_cast<std::size_t> (numGrid);
    levelPrefix.assign (gridSize + 1, 0.0);
    laneLevel.assign (static_cast<std::size_t> (kMaxChannels) * gridSize, 0.0f);
    linkLevel.assign (static_cast<std::size_t> (kMaxChannels) * gridSize, 0.0f);
    maskScratch.assign (gridSize, 1.0f);
    channelMask.assign (static_cast<std::size_t> (kMaxChannels) * gridSize, 0.0f);
    midMask.assign (static_cast<std::size_t> (kMaxChannels / 2) * gridSize, 0.0f);
    sideMask.assign (static_cast<std::size_t> (kMaxChannels / 2) * gridSize, 0.0f);
    for (auto& state : states)
    {
        state.weights.assign (gridSize, 0.0f);
        state.gains.assign (static_cast<std::size_t> (kMaxChannels) * gridSize, 0.0f);
    }

    configureSize (1);
    reset();
}

bool SpectralEngine::setQuality (Quality q) noexcept
{
    const int index = q == Quality::Eco ? 0 : (q == Quality::Normal ? 1 : 2);
    const bool changed = index != sizeIndex || hopFor (q, sizes[static_cast<std::size_t> (index)]) != hop;
    quality = q;
    if (changed)
    {
        configureSize (index);
        reset();
    }
    return changed;
}

void SpectralEngine::configureSize (int index) noexcept
{
    sizeIndex = index;
    fftSize = sizes[static_cast<std::size_t> (index)];
    hop = hopFor (quality, fftSize);

    // Bin → grid interpolation for this frame size.
    const double binHz = fs / fftSize;
    const int bins = fftSize / 2 + 1;
    int g = 0;
    for (int k = 0; k < bins; ++k)
    {
        const double erb = erbNumber (k * binHz);
        while (g + 1 < numGrid && grid[static_cast<std::size_t> (g + 1)].erb <= erb)
            ++g;
        float frac = 0.0f;
        if (g + 1 < numGrid && erb > grid[static_cast<std::size_t> (g)].erb)
            frac = static_cast<float> ((erb - grid[static_cast<std::size_t> (g)].erb)
                                       / (grid[static_cast<std::size_t> (g + 1)].erb - grid[static_cast<std::size_t> (g)].erb));
        binGridIndex[static_cast<std::size_t> (k)] = g;
        binGridFrac[static_cast<std::size_t> (k)] = std::clamp (frac, 0.0f, 1.0f);
    }
}

void SpectralEngine::reset() noexcept
{
    for (auto& r : inputRing)
        std::fill (r.begin(), r.end(), 0.0);
    for (auto& a : outputAccum)
        std::fill (a.begin(), a.end(), 0.0f);
    ringPos = 0;
    hopCounter = 0;
    for (auto& state : states)
    {
        std::fill (state.gains.begin(), state.gains.end(), 0.0f);
        state.lanes = {};
        state.weightsValid = false;
    }
    channelSpectral.fill (false);
    stftAmount.fill (0.0f);
    for (auto& g : bandGains)
        g.store (0.0f, std::memory_order_relaxed);
}

void SpectralEngine::process (double* const* io, int numChannels, int numSamples, const engine::BandSettingsArray& bands,
                              const engine::ChannelTopology& topology) noexcept
{
    numChannels = std::min (numChannels, channels);
    const int mask = fftSize - 1;
    const float rampStep = 1.0f / static_cast<float> (hop);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t> (ch);
            auto& ring = inputRing[c];
            auto& accum = outputAccum[c];
            const auto pos = static_cast<std::size_t> (ringPos);

            const double delayed = ring[pos]; // the input one frame ago: exact bypass with the same latency
            ring[pos] = io[ch][i];
            const float processed = accum[pos];
            accum[pos] = 0.0f;

            // Channels move between bypass and STFT with a one-hop crossfade.
            auto& amount = stftAmount[c];
            amount = channelSpectral[c] ? std::min (1.0f, amount + rampStep) : std::max (0.0f, amount - rampStep);
            io[ch][i] = amount <= 0.0f ? delayed : (amount >= 1.0f ? processed : delayed + (processed - delayed) * amount);
        }

        ringPos = (ringPos + 1) & mask;
        if (++hopCounter >= hop)
        {
            hopCounter = 0;
            runFrame (bands, topology);
        }
    }
}

void SpectralEngine::updateWeights (BandState& state, const filters::BandShape& shape) noexcept
{
    constexpr std::equal_to<double> same;
    if (state.weightsValid && state.weightShape.shape == shape.shape && state.weightShape.slope == shape.slope
        && same (state.weightShape.frequencyHz, shape.frequencyHz) && same (state.weightShape.q, shape.q))
        return;

    state.weightShape = shape;
    state.weightsValid = true;

    // The band's own shape at a reference gain, normalised to 1 at its centre or plateau.
    auto reference = shape;
    reference.gainDb = kWeightReferenceDb;
    const auto design = filters::designBand (reference, fs);
    for (int i = 0; i < numGrid; ++i)
    {
        const double hz = grid[static_cast<std::size_t> (i)].hz;
        const double db = gainToDb (std::abs (design.digitalResponse (std::min (hz, 0.49 * fs), fs)));
        state.weights[static_cast<std::size_t> (i)] = static_cast<float> (std::clamp (db / kWeightReferenceDb, 0.0, 1.0));
    }
}

void SpectralEngine::laneLevels (const engine::Lane& lane, const engine::BandSettings& band, float* out) noexcept
{
    const int bins = fftSize / 2 + 1;
    const double binHz = fs / fftSize;

    // Power per bin of the lane's spectrum, as a prefix sum.
    const float* a = spectra[static_cast<std::size_t> (lane.a)].data();
    const float* b = lane.b >= 0 ? spectra[static_cast<std::size_t> (lane.b)].data() : nullptr;
    powerPrefix[0] = 0.0;
    for (int k = 0; k < bins; ++k)
    {
        double re = a[2 * k], im = a[2 * k + 1];
        if (lane.kind == engine::Lane::Kind::Mid)
        {
            re = 0.5 * (re + b[2 * k]);
            im = 0.5 * (im + b[2 * k + 1]);
        }
        else if (lane.kind == engine::Lane::Kind::Side)
        {
            re = 0.5 * (re - b[2 * k]);
            im = 0.5 * (im - b[2 * k + 1]);
        }
        powerPrefix[static_cast<std::size_t> (k + 1)] = powerPrefix[static_cast<std::size_t> (k)] + re * re + im * im;
    }

    // Level within the detection band around each grid point, calibrated to dBFS of a sine.
    const double halfWidth = 0.5 * detectionWidthErb (band.dynamics.selectivity);
    const double calibration = 4.0 / (fftSize * windowPower[static_cast<std::size_t> (sizeIndex)]);
    for (int i = 0; i < numGrid; ++i)
    {
        const auto& g = grid[static_cast<std::size_t> (i)];
        const double centre = g.hz / binHz;
        double lo = erbToHz (g.erb - halfWidth) / binHz;
        double hi = erbToHz (g.erb + halfWidth) / binHz;
        lo = std::min (lo, centre - 1.5); // at least the main lobe of the window
        hi = std::max (hi, centre + 1.5);
        const int k0 = std::clamp (static_cast<int> (std::ceil (lo)), 0, bins - 1);
        const int k1 = std::clamp (static_cast<int> (std::floor (hi)), k0, bins - 1);
        const double power = (powerPrefix[static_cast<std::size_t> (k1 + 1)] - powerPrefix[static_cast<std::size_t> (k0)]) * calibration;
        out[i] = power > 1.0e-20 ? static_cast<float> (10.0 * std::log10 (power)) : kSilenceDb;
    }

    // Relative threshold: level above the mean level within ±2 ERB (DYN-10).
    if (band.dynamics.thresholdMode == ThresholdMode::Relative)
    {
        levelPrefix[0] = 0.0;
        for (int i = 0; i < numGrid; ++i)
            levelPrefix[static_cast<std::size_t> (i + 1)] = levelPrefix[static_cast<std::size_t> (i)] + out[i];
        const int half = static_cast<int> (kRelativeHalfWidthErb * kPointsPerErb);
        for (int i = 0; i < numGrid; ++i)
        {
            const int i0 = std::max (0, i - half);
            const int i1 = std::min (numGrid - 1, i + half);
            const double mean =
                (levelPrefix[static_cast<std::size_t> (i1 + 1)] - levelPrefix[static_cast<std::size_t> (i0)]) / (i1 - i0 + 1);
            maskScratch[static_cast<std::size_t> (i)] = static_cast<float> (out[i] - mean);
        }
        std::copy (maskScratch.begin(), maskScratch.begin() + numGrid, out);
    }
}

void SpectralEngine::applyMask (const float* maskDb, float* gainsOut) noexcept
{
    for (int i = 0; i < numGrid; ++i)
        maskScratch[static_cast<std::size_t> (i)] = static_cast<float> (dbToGain (static_cast<double> (maskDb[i])));
    const int bins = fftSize / 2 + 1;
    for (int k = 0; k < bins; ++k)
    {
        const int g = binGridIndex[static_cast<std::size_t> (k)];
        const float t = binGridFrac[static_cast<std::size_t> (k)];
        const float a = maskScratch[static_cast<std::size_t> (g)];
        const float b = maskScratch[static_cast<std::size_t> (std::min (g + 1, numGrid - 1))];
        gainsOut[k] = a + (b - a) * t;
    }
}

void SpectralEngine::runFrame (const engine::BandSettingsArray& bands, const engine::ChannelTopology& topology) noexcept
{
    const auto gridSize = static_cast<std::size_t> (numGrid);
    const auto& window = windows[static_cast<std::size_t> (sizeIndex)];
    auto& fft = ffts[static_cast<std::size_t> (sizeIndex)];
    const int mask = fftSize - 1;

    // Analysis of every channel (frames are chronological from the oldest sample).
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto& ring = inputRing[static_cast<std::size_t> (ch)];
        for (int j = 0; j < fftSize; ++j)
            frame[static_cast<std::size_t> (j)] =
                static_cast<float> (ring[static_cast<std::size_t> ((ringPos + j) & mask)]) * window[static_cast<std::size_t> (j)];
        fft.forward (frame.data(), spectra[static_cast<std::size_t> (ch)].data());
    }

    std::fill (channelMask.begin(), channelMask.end(), 0.0f);
    std::fill (midMask.begin(), midMask.end(), 0.0f);
    std::fill (sideMask.begin(), sideMask.end(), 0.0f);
    channelHasMask.fill (false);
    pairHasMid.fill (false);
    pairHasSide.fill (false);
    std::array<bool, kMaxChannels> spectralNow {};

    auto& view = maskViews->writeBuffer();
    view.numPoints = numGrid;
    for (int i = 0; i < numGrid; ++i)
        view.hz[static_cast<std::size_t> (i)] = static_cast<float> (grid[static_cast<std::size_t> (i)].hz);

    const double hopSeconds = static_cast<double> (hop) / fs;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& band = bands[static_cast<std::size_t> (b)];
        auto& state = states[static_cast<std::size_t> (b)];
        auto& bandView = view.bandDb[static_cast<std::size_t> (b)];
        view.bandActive[static_cast<std::size_t> (b)] = band.isSpectral() && band.active;
        if (! view.bandActive[static_cast<std::size_t> (b)])
        {
            if (state.lanes.numLanes > 0)
            {
                std::fill (state.gains.begin(), state.gains.end(), 0.0f);
                state.lanes = {};
            }
            bandGains[static_cast<std::size_t> (b)].store (0.0f, std::memory_order_relaxed);
            continue;
        }
        std::fill (bandView.begin(), bandView.begin() + numGrid, 0.0f);

        updateWeights (state, band.shape);
        const auto lanes = engine::makeLanes (band.component, band.channelMask, band.includeLfe, topology);
        if (! (lanes == state.lanes))
        {
            state.lanes = lanes;
            std::fill (state.gains.begin(), state.gains.end(), 0.0f);
        }

        const auto& dyn = band.dynamics;
        const auto computer = dynamics::GainComputer::make (dyn.thresholdDb, dyn.ratio, dyn.kneeDb, dyn.rangeDb, dyn.direction);
        const auto attack = static_cast<float> (dynamics::onePoleCoefficient (0.001 * dyn.attackMs, 1.0 / hopSeconds));
        const auto release = static_cast<float> (dynamics::onePoleCoefficient (0.001 * dyn.releaseMs, 1.0 / hopSeconds));
        const auto mix = static_cast<float> (std::clamp (dyn.mix, 0.0, 1.0));

        // Levels per lane, then the loudest level of each link set.
        const int numLanes = lanes.numLanes;
        std::array<int, kMaxChannels> representative {};
        for (int l = 0; l < numLanes; ++l)
        {
            const auto& lane = lanes.lanes[static_cast<std::size_t> (l)];
            laneLevels (lane, band, laneLevel.data() + static_cast<std::size_t> (l) * gridSize);
            const int key = engine::linkKey (lane, l, dyn.link, topology);
            representative[static_cast<std::size_t> (l)] = l;
            for (int m = 0; m < l; ++m)
                if (engine::linkKey (lanes.lanes[static_cast<std::size_t> (m)], m, dyn.link, topology) == key)
                {
                    representative[static_cast<std::size_t> (l)] = m;
                    break;
                }
        }
        std::copy (laneLevel.begin(), laneLevel.begin() + static_cast<std::ptrdiff_t> (numLanes * numGrid), linkLevel.begin());
        for (int l = 0; l < numLanes; ++l)
        {
            const auto rep = static_cast<std::size_t> (representative[static_cast<std::size_t> (l)]);
            if (rep == static_cast<std::size_t> (l))
                continue;
            float* target = linkLevel.data() + rep * gridSize;
            const float* source = laneLevel.data() + static_cast<std::size_t> (l) * gridSize;
            for (std::size_t i = 0; i < gridSize; ++i)
                target[i] = std::max (target[i], source[i]);
        }

        float extreme = 0.0f;
        for (int l = 0; l < numLanes; ++l)
        {
            const auto& lane = lanes.lanes[static_cast<std::size_t> (l)];
            const float* level = linkLevel.data() + static_cast<std::size_t> (representative[static_cast<std::size_t> (l)]) * gridSize;
            float* gains = state.gains.data() + static_cast<std::size_t> (l) * gridSize;

            float* maskDb = nullptr;
            if (lane.kind == engine::Lane::Kind::Channel)
            {
                maskDb = channelMask.data() + static_cast<std::size_t> (lane.a) * gridSize;
                channelHasMask[static_cast<std::size_t> (lane.a)] = true;
                spectralNow[static_cast<std::size_t> (lane.a)] = true;
            }
            else
            {
                const int pair = topology.pairIndex[static_cast<std::size_t> (lane.a)];
                const bool mid = lane.kind == engine::Lane::Kind::Mid;
                maskDb = (mid ? midMask.data() : sideMask.data()) + static_cast<std::size_t> (pair) * gridSize;
                (mid ? pairHasMid : pairHasSide)[static_cast<std::size_t> (pair)] = true;
                spectralNow[static_cast<std::size_t> (lane.a)] = true;
                spectralNow[static_cast<std::size_t> (lane.b)] = true;
            }

            for (std::size_t i = 0; i < gridSize; ++i)
            {
                const float weight = state.weights[i];
                const float target = weight > 1.0e-4f ? weight * static_cast<float> (computer (level[i])) : 0.0f;
                float& g = gains[i];
                const float a = std::abs (target) > std::abs (g) ? attack : release;
                g = a * g + (1.0f - a) * target;
                const float applied = mix * g;
                maskDb[i] += applied;
                if (std::abs (applied) > std::abs (bandView[i]))
                    bandView[i] = applied;
                if (std::abs (applied) > std::abs (extreme))
                    extreme = applied;
            }
        }
        bandGains[static_cast<std::size_t> (b)].store (extreme, std::memory_order_relaxed);
    }
    channelSpectral = spectralNow;
    maskViews->publish();

    // Apply: Mid/Side masks per pair, then the channel masks.
    const int bins = fftSize / 2 + 1;
    for (int p = 0; p < topology.numPairs; ++p)
    {
        const auto pi = static_cast<std::size_t> (p);
        if (! pairHasMid[pi] && ! pairHasSide[pi])
            continue;
        const auto& pair = topology.pairs[pi];
        if (pair.left >= channels || pair.right >= channels)
            continue;
        applyMask (midMask.data() + pi * gridSize, binGains.data());
        applyMask (sideMask.data() + pi * gridSize, binGainsB.data());
        float* l = spectra[static_cast<std::size_t> (pair.left)].data();
        float* r = spectra[static_cast<std::size_t> (pair.right)].data();
        for (int k = 0; k < bins; ++k)
            for (int part = 0; part < 2; ++part)
            {
                const auto idx = static_cast<std::size_t> (2 * k + part);
                const float m = 0.5f * (l[idx] + r[idx]) * binGains[static_cast<std::size_t> (k)];
                const float s = 0.5f * (l[idx] - r[idx]) * binGainsB[static_cast<std::size_t> (k)];
                l[idx] = m + s;
                r[idx] = m - s;
            }
    }
    for (int ch = 0; ch < channels; ++ch)
    {
        if (! channelHasMask[static_cast<std::size_t> (ch)])
            continue;
        applyMask (channelMask.data() + static_cast<std::size_t> (ch) * gridSize, binGains.data());
        float* x = spectra[static_cast<std::size_t> (ch)].data();
        for (int k = 0; k < bins; ++k)
        {
            x[2 * k] *= binGains[static_cast<std::size_t> (k)];
            x[2 * k + 1] *= binGains[static_cast<std::size_t> (k)];
        }
    }

    // Synthesis: inverse FFT, synthesis window, weighted overlap-add (Σ hann = N / (2·hop)).
    const auto scale = static_cast<float> (2.0 * hop / fftSize);
    for (int ch = 0; ch < channels; ++ch)
    {
        fft.inverse (spectra[static_cast<std::size_t> (ch)].data(), frame.data());
        auto& accum = outputAccum[static_cast<std::size_t> (ch)];
        for (int j = 0; j < fftSize; ++j)
            accum[static_cast<std::size_t> ((ringPos + j) & mask)] +=
                frame[static_cast<std::size_t> (j)] * window[static_cast<std::size_t> (j)] * scale;
    }
}
} // namespace ssc::spectral
