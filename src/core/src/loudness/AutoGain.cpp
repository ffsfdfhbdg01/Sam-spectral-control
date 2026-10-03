#include "ssc/loudness/AutoGain.h"

#include <algorithm>
#include <cmath>

#include "ssc/util/Math.h"

namespace ssc::loudness
{
double autoGainSeconds (AutoGainSpeed speed) noexcept
{
    switch (speed)
    {
        case AutoGainSpeed::VeryFast: return 0.05;
        case AutoGainSpeed::Fast: return 0.4;
        case AutoGainSpeed::Medium: return 3.0;
        case AutoGainSpeed::Long: return 12.0;
        case AutoGainSpeed::Stable: return 30.0;
    }
    return 30.0;
}

void AutoGain::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);

    for (int i = 0; i < 2; ++i)
    {
        auto& a = analyses[static_cast<std::size_t> (i)];
        const double nominal = i == 0 ? 4096.0 : 1024.0;
        a.fftSize = nextPowerOfTwo (static_cast<int> (std::lround (nominal * fs / 48000.0 * 0.75)) + 1);
        a.hop = a.fftSize / 4;
        a.fft.prepare (a.fftSize);
        a.window.resize (static_cast<std::size_t> (a.fftSize));
        a.windowPower = 0.0;
        for (int n = 0; n < a.fftSize; ++n)
        {
            const double w = 0.5 - 0.5 * std::cos (kTwoPi * n / a.fftSize);
            a.window[static_cast<std::size_t> (n)] = static_cast<float> (w);
            a.windowPower += w * w;
        }
        a.bands.prepare (fs, a.fftSize);
    }

    ringSize = analyses[0].fftSize;
    frame.assign (static_cast<std::size_t> (ringSize), 0.0f);
    spectrum.assign (static_cast<std::size_t> (ringSize + 2), 0.0f);

    const auto numBands = static_cast<std::size_t> (analyses[0].bands.numBands());
    const auto ch = static_cast<std::size_t> (channels);
    inputRing.assign (ch, std::vector<double> (static_cast<std::size_t> (ringSize), 0.0));
    outputRing.assign (ch, std::vector<double> (static_cast<std::size_t> (ringSize), 0.0));
    frameIn.assign (numBands, 0.0);
    frameOut.assign (numBands, 0.0);
    averageIn.assign (numBands, 0.0);
    averageOut.assign (numBands, 0.0);
    estimate.assign (numBands, 0.0);
    frameInPerChannel.assign (ch, std::vector<double> (numBands, 0.0));
    longTermIn.assign (ch, std::vector<double> (numBands, 0.0));
    staticCurve.assign (ch, std::vector<double> (numBands, 1.0));
    reset();
}

void AutoGain::reset() noexcept
{
    for (auto& r : inputRing)
        std::fill (r.begin(), r.end(), 0.0);
    for (auto& r : outputRing)
        std::fill (r.begin(), r.end(), 0.0);
    std::fill (averageIn.begin(), averageIn.end(), 0.0);
    std::fill (averageOut.begin(), averageOut.end(), 0.0);
    for (auto& a : longTermIn)
        std::fill (a.begin(), a.end(), 0.0);
    ringPos = 0;
    hopCounter = 0;
    hasAverage = false;
    hasEstimate = false;
    smoothedDb = 0.0;
    targetDb = currentDb = 0.0;
    correctionDb.store (0.0f, std::memory_order_relaxed);
}

void AutoGain::setStaticCurve (int channel, const double* gainsDb) noexcept
{
    if (channel < 0 || channel >= channels)
        return;
    auto& curve = staticCurve[static_cast<std::size_t> (channel)];
    for (std::size_t b = 0; b < curve.size(); ++b)
        curve[b] = std::pow (10.0, gainsDb[b] / 10.0);
}

void AutoGain::process (const double* const* input, double* const* output, int numChannels, int numSamples, const Settings& settings,
                        const std::array<float, kMaxChannels>& channelWeights, const std::array<bool, kMaxChannels>& covered) noexcept
{
    numChannels = std::min (numChannels, channels);

    if (settings.enabled && (! wasEnabled || settings.speed != lastSpeed))
    {
        // A fresh start: stale averages from another speed or an earlier session would mislead.
        std::fill (averageIn.begin(), averageIn.end(), 0.0);
        std::fill (averageOut.begin(), averageOut.end(), 0.0);
        for (auto& a : longTermIn)
            std::fill (a.begin(), a.end(), 0.0);
        hasAverage = false;
        hasEstimate = false;
        smoothedDb = 0.0;
    }
    wasEnabled = settings.enabled;
    lastSpeed = settings.speed;
    if (! settings.enabled)
        targetDb = 0.0;

    // At most 0.5 dB/ms, with a short one-pole on top so steps are smooth (AG-5).
    const double maxStep = 0.5 * 1000.0 / fs;
    const double smoothing = 1.0 - std::exp (-1.0 / (0.005 * fs));
    const int mask = ringSize - 1;
    const int hop = analysisFor (settings.speed).hop;

    for (int i = 0; i < numSamples; ++i)
    {
        if (settings.enabled)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (channelWeights[static_cast<std::size_t> (ch)] <= 0.0f)
                    continue;
                inputRing[static_cast<std::size_t> (ch)][static_cast<std::size_t> (ringPos)] = input[ch][i];
                outputRing[static_cast<std::size_t> (ch)][static_cast<std::size_t> (ringPos)] = output[ch][i];
            }
            ringPos = (ringPos + 1) & mask;
            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                analyseFrame (settings, channelWeights, covered);
            }
        }

        currentDb += std::clamp ((targetDb - currentDb) * smoothing, -maxStep, maxStep);
        if (std::abs (currentDb) > 1.0e-6)
        {
            const double g = dbToGain (currentDb);
            for (int ch = 0; ch < numChannels; ++ch)
                if (covered[static_cast<std::size_t> (ch)])
                    output[ch][i] *= g;
        }
    }
    correctionDb.store (static_cast<float> (currentDb), std::memory_order_relaxed);
}

void AutoGain::analyseFrame (const Settings& settings, const std::array<float, kMaxChannels>& channelWeights,
                             const std::array<bool, kMaxChannels>& covered) noexcept
{
    auto& analysis = analyses[settings.speed == AutoGainSpeed::VeryFast ? 1 : 0];
    const auto& bands = analysis.bands;
    const auto numBands = static_cast<std::size_t> (bands.numBands());
    const int size = analysis.fftSize;
    const int mask = ringSize - 1;
    const bool stable = settings.speed == AutoGainSpeed::Stable;
    std::fill (frameIn.begin(), frameIn.end(), 0.0);
    std::fill (frameOut.begin(), frameOut.end(), 0.0);

    // The newest `size` samples of the ring, oldest first.
    const int start = ringPos - size;
    auto analyse = [&] (const std::vector<double>& ring) {
        for (int j = 0; j < size; ++j)
            frame[static_cast<std::size_t> (j)] =
                static_cast<float> (ring[static_cast<std::size_t> ((start + j) & mask)]) * analysis.window[static_cast<std::size_t> (j)];
        analysis.fft.forward (frame.data(), spectrum.data());
    };

    // Band powers of the input of every measured channel; the output only where it is compared.
    double allPower = 0.0, coveredPower = 0.0, allWeights = 0.0, coveredWeights = 0.0;
    bool anyCovered = false;
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<std::size_t> (ch);
        const double weight = channelWeights[c];
        if (weight <= 0.0)
            continue;
        allWeights += weight;

        analyse (inputRing[c]);
        auto& perChannel = frameInPerChannel[c];
        std::fill (perChannel.begin(), perChannel.end(), 0.0);
        bands.accumulate (spectrum.data(), analysis.windowPower, 1.0, perChannel.data());
        double power = 0.0;
        for (std::size_t b = 0; b < numBands; ++b)
            power += perChannel[b];
        allPower += weight * power;

        if (! covered[c])
            continue;
        anyCovered = true;
        coveredWeights += weight;
        coveredPower += weight * power;
        for (std::size_t b = 0; b < numBands; ++b)
            frameIn[b] += weight * perChannel[b];
        if (! stable)
        {
            analyse (outputRing[c]);
            bands.accumulate (spectrum.data(), analysis.windowPower, weight, frameOut.data());
        }
    }

    // Gate: silence (below −70 dBFS; a full-scale sine has a mean square of 0.5) freezes the
    // measurement and holds the correction (AG-5).
    const double gateLevel = 0.5 * std::pow (10.0, kGateDbFs / 10.0);
    const double alpha = std::exp (-static_cast<double> (analysis.hop) / (autoGainSeconds (settings.speed) * fs));

    if (stable)
    {
        if (allPower < gateLevel * std::max (1.0, allWeights))
            return;
        const double a = hasAverage ? alpha : 0.0;
        std::fill (averageIn.begin(), averageIn.end(), 0.0);
        std::fill (estimate.begin(), estimate.end(), 0.0);
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<std::size_t> (ch);
            if (channelWeights[c] <= 0.0f)
                continue;
            auto& longTerm = longTermIn[c];
            const auto& current = frameInPerChannel[c];
            for (std::size_t b = 0; b < numBands; ++b)
                longTerm[b] = a * longTerm[b] + (1.0 - a) * current[b];
            if (! covered[c])
                continue;
            // Long-term input spectrum, alone and through the current EQ curve (AG-4: no pumping).
            const double weight = channelWeights[c];
            const auto& curve = staticCurve[c];
            for (std::size_t b = 0; b < numBands; ++b)
            {
                averageIn[b] += weight * longTerm[b];
                estimate[b] += weight * longTerm[b] * curve[b];
            }
        }
        hasAverage = true;
        const double correction = anyCovered
            ? loudnessOf (averageIn.data(), bands, settings.weighting) - loudnessOf (estimate.data(), bands, settings.weighting)
            : 0.0;
        targetDb = std::clamp (correction, -kMaxCorrectionDb, kMaxCorrectionDb);
        return;
    }

    if (! anyCovered)
    {
        targetDb = smoothedDb = 0.0;
        hasEstimate = false;
        return;
    }
    if (coveredPower < gateLevel * std::max (1.0, coveredWeights))
        return;

    // Loudness difference of this frame, smoothed in dB so the step response is exponential
    // with the time constant of the speed (AG-4).
    const double difference =
        loudnessOf (frameIn.data(), bands, settings.weighting) - loudnessOf (frameOut.data(), bands, settings.weighting);
    smoothedDb = hasEstimate ? alpha * smoothedDb + (1.0 - alpha) * difference : difference;
    hasEstimate = true;
    targetDb = std::clamp (smoothedDb, -kMaxCorrectionDb, kMaxCorrectionDb);
}

} // namespace ssc::loudness
