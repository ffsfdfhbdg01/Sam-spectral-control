#include "ssc/phase/PhaseCorrector.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "ssc/util/Math.h"

namespace ssc::phase
{
namespace
{
    constexpr double kKaiserBeta = 4.0;       ///< chosen on the PH-1 reference set (4 … 8 tried)
    constexpr double kBrickwallBeta = 8.0;    ///< ≈ 80 dB stopband for filters with a brickwall cut
    constexpr double kCorrectionLimit = 0.95; ///< of Nyquist; the correction fades out above
    constexpr double kMagnitudeFloor = 1e-4;  ///< −80 dB: below, a band's magnitude is not corrected
    constexpr double kMaxCorrectionDb = 12.0; ///< limit of the magnitude correction of a channel

    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 500; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-20 * sum)
                break;
        }
        return sum;
    }

    int qualityIndex (Quality q) noexcept
    {
        return std::clamp (static_cast<int> (q), 0, 3);
    }

    int gridSizeFor (int delay) noexcept
    {
        return std::clamp (nextPowerOfTwo (8 * (2 * delay + 1)), 512, 8192);
    }

    /** Same band as far as the correction is concerned (dynamic gains within 0.05 dB). */
    bool sameShape (const filters::BandShape& a, const filters::BandShape& b) noexcept
    {
        constexpr std::equal_to<double> same;
        return a.shape == b.shape && a.slope == b.slope && a.character == b.character && a.analog == b.analog
            && same (a.frequencyHz, b.frequencyHz) && same (a.q, b.q) && std::abs (a.gainDb - b.gainDb) < 0.05;
    }

    /** Keeps a numerator/denominator pair of long products in range without changing their ratio. */
    void renormalise (std::complex<double>& num, std::complex<double>& den) noexcept
    {
        const double scale = std::abs (num) + std::abs (den);
        if (scale > 1e100 || (scale > 0.0 && scale < 1e-100))
        {
            num /= scale;
            den /= scale;
        }
    }

    double dot (const double* a, const double* b, int n) noexcept
    {
        double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
        int i = 0;
        for (; i + 4 <= n; i += 4)
        {
            s0 += a[i] * b[i];
            s1 += a[i + 1] * b[i + 1];
            s2 += a[i + 2] * b[i + 2];
            s3 += a[i + 3] * b[i + 3];
        }
        for (; i < n; ++i)
            s0 += a[i] * b[i];
        return (s0 + s1) + (s2 + s3);
    }
} // namespace

int correctorDelayFor (Quality quality, double sampleRate) noexcept
{
    static constexpr int kDelays48k[] = { 16, 32, 64, 128 };
    return std::max (1, static_cast<int> (std::lround (kDelays48k[qualityIndex (quality)] * sampleRate / 48000.0)));
}

double minimumFirBrickwallHz (Quality quality, double sampleRate) noexcept
{
    return 7.0 * sampleRate / (2 * correctorDelayFor (quality, sampleRate) + 1);
}

std::vector<double> brickwallTaps (bool lowCut, double cornerHz, Quality quality, double sampleRate)
{
    // The windowed ideal cut: the same window as the corrector uses for filters with a brickwall.
    const int d = correctorDelayFor (quality, sampleRate);
    const double cutoff = std::clamp (cornerHz / sampleRate, 0.0, 0.5);
    std::vector<double> taps (static_cast<std::size_t> (2 * d + 1));
    for (int n = 0; n <= 2 * d; ++n)
    {
        const int k = n - d;
        const double lowPass = k == 0 ? 2.0 * cutoff : std::sin (kTwoPi * cutoff * k) / (kPi * k);
        const double r = static_cast<double> (k) / d;
        const double window = besselI0 (kBrickwallBeta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (kBrickwallBeta);
        taps[static_cast<std::size_t> (n)] = window * (lowCut ? (k == 0 ? 1.0 : 0.0) - lowPass : lowPass);
    }
    return taps;
}

void PhaseCorrector::prepare (double sampleRate, int channels, int maxBlockSize)
{
    fs = sampleRate;
    numChannels = std::clamp (channels, 1, kMaxChannels);
    maxBlock = std::max (1, maxBlockSize);
    maxDelay = correctorDelayFor (Quality::Max, fs);
    maxLength = 2 * maxDelay + 1;
    updateInterval = nextPowerOfTwo (static_cast<int> (std::lround (0.02 * fs)));
    fadeLength = std::max (32, static_cast<int> (std::lround (0.005 * fs)));

    int maxGrid = 0;
    for (int q = 0; q < 4; ++q)
    {
        gridSizes[static_cast<std::size_t> (q)] = gridSizeFor (correctorDelayFor (static_cast<Quality> (q), fs));
        ffts[static_cast<std::size_t> (q)].prepare (gridSizes[static_cast<std::size_t> (q)]);
        maxGrid = std::max (maxGrid, gridSizes[static_cast<std::size_t> (q)]);
    }

    const auto points = static_cast<std::size_t> (maxGrid / 2 + 1);
    window.assign (static_cast<std::size_t> (maxLength), 0.0);
    brickwallWindow.assign (static_cast<std::size_t> (maxLength), 0.0);
    minimumPhaseWindow.assign (static_cast<std::size_t> (maxLength), 0.0);
    delayPhase.assign (points, {});
    zInverse.assign (points, {});
    spectrum.assign (static_cast<std::size_t> (maxGrid + 2), 0.0f);
    time.assign (static_cast<std::size_t> (maxGrid), 0.0f);
    tapsA.assign (static_cast<std::size_t> (maxLength), 0.0);
    tapsB.assign (static_cast<std::size_t> (maxLength), 0.0);
    factors.assign (static_cast<std::size_t> (kNumBands), std::vector<std::complex<double>> (points));

    state.assign (static_cast<std::size_t> (numChannels), Channel {});
    for (auto& ch : state)
    {
        ch.history.assign (static_cast<std::size_t> (maxLength - 1 + maxBlock), 0.0);
        for (auto* taps : { &ch.self, &ch.cross, &ch.oldSelf, &ch.oldCross })
            taps->assign (static_cast<std::size_t> (maxLength), 0.0);
    }

    delay = -1; // force the configuration below
    setQuality (Quality::Normal);
}

bool PhaseCorrector::setQuality (Quality q, bool minimum) noexcept
{
    const int d = correctorDelayFor (q, fs);
    const int newDelay = minimum ? 0 : d;
    if (newDelay == delay && q == quality && minimum == minimumPhase && delay >= 0)
        return false;

    quality = q;
    minimumPhase = minimum;
    delay = newDelay;
    length = minimum ? d + 1 : 2 * d + 1;
    gridSize = gridSizes[static_cast<std::size_t> (qualityIndex (q))];
    numPoints = gridSize / 2 + 1;

    for (int m = 0; m < 2 * d + 1; ++m)
    {
        const double r = static_cast<double> (m - d) / d;
        const double x = std::sqrt (std::max (0.0, 1.0 - r * r));
        window[static_cast<std::size_t> (m)] = besselI0 (kKaiserBeta * x) / besselI0 (kKaiserBeta);
        brickwallWindow[static_cast<std::size_t> (m)] = besselI0 (kBrickwallBeta * x) / besselI0 (kBrickwallBeta);
    }
    // Minimum phase: the right half of the Kaiser window over D + 1 taps (the energy is at the start).
    for (int m = 0; m <= d; ++m)
    {
        const double r = static_cast<double> (m) / (d + 1);
        minimumPhaseWindow[static_cast<std::size_t> (m)] =
            besselI0 (kKaiserBeta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (kKaiserBeta);
    }
    for (int k = 0; k < numPoints; ++k)
        delayPhase[static_cast<std::size_t> (k)] = std::polar (1.0, -kTwoPi * static_cast<double> (k) * delay / gridSize);

    // Everything is rebuilt on the next update; until then every channel is a pure delay.
    factorValid.fill (false);
    rate = 0.0;
    for (auto& ch : state)
    {
        ch.selfDelay = ch.crossZero = true;
        ch.fade = 0;
        ch.partner = -1;
        ch.builtA = ch.builtB = 0;
        ch.builtVersion = 0;
        ch.builtMs = false;
    }
    reset();
    return true;
}

void PhaseCorrector::reset() noexcept
{
    for (auto& ch : state)
    {
        std::fill (ch.history.begin(), ch.history.end(), 0.0);
        ch.fade = 0;
    }
}

bool PhaseCorrector::isCorrecting() const noexcept
{
    return std::any_of (state.begin(), state.end(), [] (const Channel& ch) { return ch.fade > 0 || ! ch.selfDelay || ! ch.crossZero; });
}

std::uint64_t PhaseCorrector::versionOf (std::uint32_t mask) const noexcept
{
    std::uint64_t sum = 0;
    for (int b = 0; b < kNumBands; ++b)
        if ((mask >> b) & 1u)
            sum += versions[static_cast<std::size_t> (b)];
    return sum;
}

void PhaseCorrector::computeBandFactor (int band, const CorrectorBand& settings) noexcept
{
    auto& f = factors[static_cast<std::size_t> (band)];
    const double hzPerPoint = fs / gridSize;

    if (settings.brickwall)
    {
        // Ideal linear-phase cut; its slope follows from the window (−6 dB at the corner).
        const bool lowCut = settings.shape.shape == Shape::LowCut;
        const double corner = settings.shape.frequencyHz;
        for (int k = 0; k < numPoints; ++k)
        {
            const double hz = k * hzPerPoint;
            double v = (hz > corner) == lowCut ? 1.0 : 0.0;
            if (std::abs (hz - corner) < 1e-9)
                v = 0.5;
            f[static_cast<std::size_t> (k)] = { v, 0.0 };
        }
        return;
    }

    // T/A per grid point. Where the band is far down (stopbands, notches) only its phase is used.
    const auto design = filters::designBand (settings.shape, rate);
    for (int k = 0; k < numPoints; ++k)
    {
        const double omega = kTwoPi * k * hzPerPoint;
        const double w2 = omega * omega;
        const auto z1 = zInverse[static_cast<std::size_t> (k)];
        const auto z2 = z1 * z1;
        std::complex<double> analogNum (1.0, 0.0), analogDen (1.0, 0.0), digitalNum (1.0, 0.0), digitalDen (1.0, 0.0);
        for (int s = 0; s < design.numSections; ++s)
        {
            const auto& a = design.analog[static_cast<std::size_t> (s)];
            const auto& d = design.digital[static_cast<std::size_t> (s)];
            analogNum *= std::complex<double> (a.n0 - a.n2 * w2, a.n1 * omega);
            analogDen *= std::complex<double> (a.d0 - a.d2 * w2, a.d1 * omega);
            digitalNum *= d.b0 + d.b1 * z1 + d.b2 * z2;
            digitalDen *= 1.0 + d.a1 * z1 + d.a2 * z2;
            renormalise (analogNum, analogDen);
            renormalise (digitalNum, digitalDen);
        }

        std::complex<double> r (1.0, 0.0);
        const double tDen = std::abs (analogDen), aDen = std::abs (digitalDen);
        if (tDen > 0.0 && aDen > 0.0)
        {
            const auto t = analogNum / analogDen;
            const auto a = digitalNum / digitalDen;
            const double magT = std::abs (t), magA = std::abs (a);
            if (magT > 1e-280 && magA > 1e-280 && std::isfinite (magT) && std::isfinite (magA))
            {
                r = t / a;
                if (magT < kMagnitudeFloor || magA < kMagnitudeFloor)
                    r /= std::abs (r); // phase only
            }
        }
        f[static_cast<std::size_t> (k)] = r;
    }
}

bool PhaseCorrector::designTaps (std::uint32_t mask, std::vector<double>& reversed) noexcept
{
    if (mask == 0)
        return false;

    const double nyquist = 0.5 * fs;
    const double limit = kCorrectionLimit * nyquist;
    const double hzPerPoint = fs / gridSize;
    const double minGain = dbToGain (-kMaxCorrectionDb), maxGain = dbToGain (kMaxCorrectionDb);
    for (int k = 0; k < numPoints; ++k)
    {
        std::complex<double> correction (1.0, 0.0);
        double magnitude = 1.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            if (((mask >> b) & 1u) == 0)
                continue;
            const auto& v = factors[static_cast<std::size_t> (b)][static_cast<std::size_t> (k)];
            if (factorBrickwall[static_cast<std::size_t> (b)])
                magnitude *= v.real();
            else
                correction *= v;
        }

        // Split into magnitude (limited) and phase; both fade out towards Nyquist.
        const double ratio = std::abs (correction);
        double gain = ratio > 0.0 ? std::clamp (ratio, minGain, maxGain) : 1.0;
        auto phase = ratio > 0.0 ? correction / ratio : std::complex<double> (1.0, 0.0);
        const double hz = k * hzPerPoint;
        if (hz > limit)
        {
            const double t = std::min (1.0, (hz - limit) / (nyquist - limit));
            const double w = 0.5 - 0.5 * std::cos (kPi * t);
            const auto blended = phase * (1.0 - w) + w;
            const double m = std::abs (blended);
            phase = m > 1e-9 ? blended / m : std::complex<double> (1.0, 0.0);
            gain = std::pow (gain, 1.0 - w);
        }

        if (minimumPhase)
        {
            // Magnitude only: its log, for the cepstrum.
            spectrum[static_cast<std::size_t> (2 * k)] = static_cast<float> (std::log (magnitude * gain));
            spectrum[static_cast<std::size_t> (2 * k + 1)] = 0.0f;
            continue;
        }
        const auto c = magnitude * gain * phase * delayPhase[static_cast<std::size_t> (k)];
        spectrum[static_cast<std::size_t> (2 * k)] = static_cast<float> (c.real());
        spectrum[static_cast<std::size_t> (2 * k + 1)] = static_cast<float> (c.imag());
    }
    if (minimumPhase)
    {
        minimumPhaseTaps (reversed);
        return true;
    }
    spectrum[1] = 0.0f;
    spectrum[static_cast<std::size_t> (2 * (numPoints - 1) + 1)] = 0.0f;

    ffts[static_cast<std::size_t> (qualityIndex (quality))].inverse (spectrum.data(), time.data());

    // A brickwall needs the stopband of a steeper window more than the finest phase accuracy.
    bool hasBrickwall = false;
    for (int b = 0; b < kNumBands; ++b)
        hasBrickwall = hasBrickwall || (((mask >> b) & 1u) != 0 && factorBrickwall[static_cast<std::size_t> (b)]);
    const auto& w = hasBrickwall ? brickwallWindow : window;
    for (int m = 0; m < length; ++m)
        reversed[static_cast<std::size_t> (length - 1 - m)] =
            static_cast<double> (time[static_cast<std::size_t> (m)]) * w[static_cast<std::size_t> (m)];
    return true;
}

void PhaseCorrector::minimumPhaseTaps (std::vector<double>& reversed) noexcept
{
    // Real cepstrum of the log magnitude, folded onto positive times (the minimum-phase cepstrum),
    // back to the spectrum, exponentiated and transformed: the minimum-phase response.
    auto& fft = ffts[static_cast<std::size_t> (qualityIndex (quality))];
    fft.inverse (spectrum.data(), time.data());
    const int half = gridSize / 2;
    for (int n = 1; n < half; ++n)
        time[static_cast<std::size_t> (n)] *= 2.0f;
    std::fill (time.begin() + half + 1, time.begin() + gridSize, 0.0f);
    fft.forward (time.data(), spectrum.data());
    for (int k = 0; k < numPoints; ++k)
    {
        const auto v =
            std::exp (std::complex<double> (spectrum[static_cast<std::size_t> (2 * k)], spectrum[static_cast<std::size_t> (2 * k + 1)]));
        spectrum[static_cast<std::size_t> (2 * k)] = static_cast<float> (v.real());
        spectrum[static_cast<std::size_t> (2 * k + 1)] = static_cast<float> (v.imag());
    }
    spectrum[1] = 0.0f;
    spectrum[static_cast<std::size_t> (2 * (numPoints - 1) + 1)] = 0.0f;
    fft.inverse (spectrum.data(), time.data());
    for (int m = 0; m < length; ++m)
        reversed[static_cast<std::size_t> (length - 1 - m)] =
            static_cast<double> (time[static_cast<std::size_t> (m)]) * minimumPhaseWindow[static_cast<std::size_t> (m)];
}

void PhaseCorrector::impulseTaps (std::vector<double>& reversed) const noexcept
{
    std::fill (reversed.begin(), reversed.begin() + length, 0.0);
    reversed[static_cast<std::size_t> (length - 1 - delay)] = 1.0;
}

void PhaseCorrector::install (Channel& ch, bool selfDelay, bool crossZero) noexcept
{
    // tapsA/tapsB hold the new self/cross taps; the current ones become the fade-out filter.
    const bool wasDelay = ch.selfDelay && ch.crossZero;
    std::swap (ch.self, ch.oldSelf);
    std::swap (ch.cross, ch.oldCross);
    ch.oldSelfDelay = ch.selfDelay;
    ch.oldCrossZero = ch.crossZero;
    std::copy (tapsA.begin(), tapsA.begin() + length, ch.self.begin());
    std::copy (tapsB.begin(), tapsB.begin() + length, ch.cross.begin());
    ch.selfDelay = selfDelay;
    ch.crossZero = crossZero;
    ch.fade = (wasDelay && selfDelay && crossZero) ? 0 : fadeLength;
}

void PhaseCorrector::update (const CorrectorBands& bands, const engine::ChannelTopology& topology, double processRate) noexcept
{
    if (processRate != rate)
    {
        rate = processRate;
        const double hzPerPoint = fs / gridSize;
        for (int k = 0; k < numPoints; ++k)
            zInverse[static_cast<std::size_t> (k)] = std::polar (1.0, -kTwoPi * k * hzPerPoint / rate);
        factorValid.fill (false);
    }

    // 1. Per-band factors, recomputed only for bands that changed.
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto bi = static_cast<std::size_t> (b);
        const auto& band = bands[bi];
        if (! band.active)
        {
            if (factorValid[bi])
            {
                factorValid[bi] = false;
                ++versions[bi];
            }
            continue;
        }
        if (factorValid[bi] && factorBrickwall[bi] == band.brickwall && sameShape (factorShape[bi], band.shape))
            continue;
        factorBrickwall[bi] = band.brickwall;
        computeBandFactor (b, band);
        factorValid[bi] = true;
        factorShape[bi] = band.shape;
        ++versions[bi];
    }

    // 2. Which bands act on which channel, Mid or Side.
    std::array<bool, kMaxChannels> msPair {};
    for (const auto& band : bands)
        if (band.active)
            for (int l = 0; l < band.lanes.numLanes; ++l)
            {
                const auto& lane = band.lanes.lanes[static_cast<std::size_t> (l)];
                if (lane.kind != engine::Lane::Kind::Channel && lane.a >= 0 && topology.pairIndex[static_cast<std::size_t> (lane.a)] >= 0)
                    msPair[static_cast<std::size_t> (topology.pairIndex[static_cast<std::size_t> (lane.a)])] = true;
            }

    std::array<std::uint32_t, kMaxChannels> self {}, mid {}, side {};
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& band = bands[static_cast<std::size_t> (b)];
        if (! band.active)
            continue;
        const std::uint32_t bit = 1u << b;
        auto hasChannelLane = [&band] (int ch) {
            for (int l = 0; l < band.lanes.numLanes; ++l)
            {
                const auto& lane = band.lanes.lanes[static_cast<std::size_t> (l)];
                if (lane.kind == engine::Lane::Kind::Channel && lane.a == ch)
                    return true;
            }
            return false;
        };
        for (int l = 0; l < band.lanes.numLanes; ++l)
        {
            const auto& lane = band.lanes.lanes[static_cast<std::size_t> (l)];
            if (lane.a < 0 || lane.a >= kMaxChannels)
                continue;
            const int p = topology.pairIndex[static_cast<std::size_t> (lane.a)];
            switch (lane.kind)
            {
                case engine::Lane::Kind::Channel:
                    if (p >= 0 && msPair[static_cast<std::size_t> (p)])
                    {
                        // In the Mid/Side basis only bands on both channels of the pair are exact.
                        const auto& pair = topology.pairs[static_cast<std::size_t> (p)];
                        if (lane.a == pair.left && hasChannelLane (pair.right))
                        {
                            mid[static_cast<std::size_t> (p)] |= bit;
                            side[static_cast<std::size_t> (p)] |= bit;
                        }
                    }
                    else
                    {
                        self[static_cast<std::size_t> (lane.a)] |= bit;
                    }
                    break;
                case engine::Lane::Kind::Mid:
                    if (p >= 0)
                        mid[static_cast<std::size_t> (p)] |= bit;
                    break;
                case engine::Lane::Kind::Side:
                    if (p >= 0)
                        side[static_cast<std::size_t> (p)] |= bit;
                    break;
            }
        }
    }

    // 3. Rebuild the filters of channels whose bands changed (not while a crossfade is running).
    for (int c = 0; c < numChannels; ++c)
    {
        const auto ci = static_cast<std::size_t> (c);
        const int p = c < topology.numChannels ? topology.pairIndex[ci] : -1;
        const bool ms = p >= 0 && msPair[static_cast<std::size_t> (p)];

        if (! ms)
        {
            auto& ch = state[ci];
            const std::uint32_t mask = self[ci];
            const auto version = versionOf (mask);
            if (! ch.builtMs && ch.builtA == mask && ch.builtVersion == version)
                continue;
            if (ch.fade > 0)
                continue;
            const bool filtered = designTaps (mask, tapsA);
            if (! filtered)
                impulseTaps (tapsA);
            std::fill (tapsB.begin(), tapsB.begin() + length, 0.0);
            install (ch, ! filtered, true);
            // The partner stays known so that a fade from a Mid/Side filter keeps its cross term.
            ch.partner = p >= 0
                ? (topology.pairs[static_cast<std::size_t> (p)].left == c ? topology.pairs[static_cast<std::size_t> (p)].right
                                                                          : topology.pairs[static_cast<std::size_t> (p)].left)
                : -1;
            ch.builtMs = false;
            ch.builtA = mask;
            ch.builtB = 0;
            ch.builtVersion = version;
            continue;
        }

        const auto& pair = topology.pairs[static_cast<std::size_t> (p)];
        if (c != pair.left || pair.right >= numChannels)
            continue; // the pair is handled with its left channel
        auto& left = state[static_cast<std::size_t> (pair.left)];
        auto& right = state[static_cast<std::size_t> (pair.right)];
        const std::uint32_t m = mid[static_cast<std::size_t> (p)], s = side[static_cast<std::size_t> (p)];
        const auto version = versionOf (m | s);
        const bool same = left.builtMs && right.builtMs && left.builtA == m && left.builtB == s && left.builtVersion == version
            && right.builtA == m && right.builtB == s && right.builtVersion == version;
        if (same || left.fade > 0 || right.fade > 0)
            continue;

        // Mid and Side filters, then P = (C_M + C_S)/2 on the own channel, Q = (C_M − C_S)/2 across.
        std::vector<double>& cm = tapsA;
        std::vector<double>& cs = tapsB;
        const bool midFiltered = designTaps (m, cm);
        if (! midFiltered)
            impulseTaps (cm);
        const bool sideFiltered = designTaps (s, cs);
        if (! sideFiltered)
            impulseTaps (cs);
        for (int i = 0; i < length; ++i)
        {
            const double a = cm[static_cast<std::size_t> (i)], b = cs[static_cast<std::size_t> (i)];
            cm[static_cast<std::size_t> (i)] = 0.5 * (a + b);
            cs[static_cast<std::size_t> (i)] = 0.5 * (a - b);
        }
        const bool pureDelay = ! midFiltered && ! sideFiltered;
        const bool noCross = m == s;
        for (auto* ch : { &left, &right })
        {
            install (*ch, pureDelay, noCross);
            ch->builtMs = true;
            ch->builtA = m;
            ch->builtB = s;
            ch->builtVersion = version;
        }
        left.partner = pair.right;
        right.partner = pair.left;
    }
}

void PhaseCorrector::process (double* const* channels, int channelsToProcess, int numSamples) noexcept
{
    const int count = std::min (channelsToProcess, numChannels);
    const int keep = maxLength - 1;
    const auto n = static_cast<std::size_t> (numSamples);

    for (int c = 0; c < count; ++c)
        std::copy (channels[c], channels[c] + numSamples, state[static_cast<std::size_t> (c)].history.begin() + keep);

    const int offset = keep - (length - 1); // window start for output sample 0
    for (int c = 0; c < count; ++c)
    {
        auto& ch = state[static_cast<std::size_t> (c)];
        double* out = channels[c];
        const double* x = ch.history.data() + offset;
        const bool hasPartner = ch.partner >= 0 && ch.partner < count;
        const double* xp = hasPartner ? state[static_cast<std::size_t> (ch.partner)].history.data() + offset : nullptr;

        if (ch.fade == 0 && ch.selfDelay && (ch.crossZero || ! hasPartner))
        {
            for (std::size_t i = 0; i < n; ++i)
                out[i] = ch.history[static_cast<std::size_t> (keep) + i - static_cast<std::size_t> (delay)]; // bit-exact delay
            continue;
        }

        auto filter = [&] (const std::vector<double>& selfTaps, bool selfDelay, const std::vector<double>& crossTaps, bool crossZero,
                           std::size_t i) {
            double y = selfDelay ? x[i + static_cast<std::size_t> (length - 1 - delay)] : dot (x + i, selfTaps.data(), length);
            if (! crossZero && hasPartner)
                y += dot (xp + i, crossTaps.data(), length);
            return y;
        };

        for (std::size_t i = 0; i < n; ++i)
        {
            double y = filter (ch.self, ch.selfDelay, ch.cross, ch.crossZero, i);
            if (ch.fade > 0)
            {
                const double w = 1.0 - static_cast<double> (ch.fade) / (fadeLength + 1);
                y = y * w + filter (ch.oldSelf, ch.oldSelfDelay, ch.oldCross, ch.oldCrossZero, i) * (1.0 - w);
                --ch.fade;
            }
            out[i] = y;
        }
    }

    for (int c = 0; c < count; ++c)
    {
        auto& h = state[static_cast<std::size_t> (c)].history;
        std::copy (h.begin() + numSamples, h.begin() + numSamples + keep, h.begin());
    }
}
} // namespace ssc::phase
