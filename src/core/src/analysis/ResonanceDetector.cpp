#include "ssc/analysis/ResonanceDetector.h"

#include <algorithm>
#include <cmath>

#include "ssc/loudness/Loudness.h"

namespace ssc::analysis
{
namespace
{
    constexpr double kGridStepOctaves = 1.0 / 24.0;
    constexpr double kEnvelopeOctaves = 1.0 / 3.0;
    constexpr int kMinEnvelopeBins = 4; ///< half width: the median needs a few bins even in the bass
    constexpr double kMatchOctaves = 1.0 / 24.0;
    constexpr double kFloorDb = -100.0; ///< no hints in (near) silence
    constexpr int kMaxPeaks = 96;
    constexpr int kSubWindows = 8;
    constexpr int kLineHalfWidth = 4;        ///< a window-shaped line falls by ≥ lineDropDb within ±4 bins (Blackman-Harris main lobe)
    constexpr double kCandidateMargin = 3.0; ///< candidates down to 3 dB below the sensitivity

    double erbHz (double hz) noexcept
    {
        return 24.7 * (4.37 * hz / 1000.0 + 1.0);
    }

    double smoothing (double dt, double tau) noexcept
    {
        return std::exp (-dt / tau);
    }
} // namespace

void ResonanceDetector::prepare (const Config& config)
{
    cfg = config;
    binHz = cfg.sampleRate / cfg.fftSize;
    const auto bins = static_cast<std::size_t> (cfg.fftSize / 2 + 1);
    levelDb.assign (bins, -200.0f);
    floorDb.assign (bins, -200.0f);
    shapeDb.assign (bins, -200.0f);
    envelopeDb.assign (bins, -200.0f);
    lineMask.assign (bins, 0);
    cumulativePower.assign (bins + 1, 0.0);
    runningMin.assign (bins, 1.0e30f);
    subMins.assign (static_cast<std::size_t> (kSubWindows), std::vector<float> (bins, 1.0e30f));
    subFrames = std::max (1, static_cast<int> (std::lround (cfg.floorSeconds / cfg.frameSeconds / kSubWindows)));
    window.assign (bins, 0.0f);

    // Envelope grid: 1/24 octave from below minHz to above maxHz.
    gridBin.clear();
    const double first = cfg.minHz * std::pow (2.0, -kEnvelopeOctaves);
    const double last = std::min (cfg.maxHz * std::pow (2.0, kEnvelopeOctaves), 0.5 * cfg.sampleRate);
    for (double hz = first; hz <= last; hz *= std::pow (2.0, kGridStepOctaves))
        gridBin.push_back (static_cast<int> (std::lround (hz / binHz)));
    gridDb.assign (gridBin.size(), -200.0f);

    peaks.assign (static_cast<std::size_t> (kMaxPeaks), {});
    tracks.assign (static_cast<std::size_t> (std::max (1, cfg.maxTracks)), {});
    visibleHints.clear();
    visibleHints.reserve (tracks.size());
    candidates.clear();
    candidates.reserve (bins / 2);
    reset();
}

void ResonanceDetector::reset() noexcept
{
    numTracks = 0;
    changes = 0;
    shown = 0;
    std::fill (runningMin.begin(), runningMin.end(), 1.0e30f);
    for (auto& m : subMins)
        std::fill (m.begin(), m.end(), 1.0e30f);
    subIndex = 0;
    framesInSub = 0;
    visibleHints.clear();
    candidates.clear();
}

double ResonanceDetector::sensitivityDb (double hz) const noexcept
{
    // How much more sensitive the ear is than at 1 kHz (ISO 226, 60 phon).
    return loudness::phonToSpl (60.0, 1000.0) - loudness::phonToSpl (60.0, std::clamp (hz, 20.0, 12500.0));
}

void ResonanceDetector::removeLines (int lo, int hi) noexcept
{
    // Steady partials have the shape of the analysis window: a local maximum that falls by at least
    // lineDropDb within its main lobe (±4 bins) on both sides. Their ±4 bins are bridged by a
    // straight line (in dB) between the bins next to them, which leaves the broadband part of the
    // frame — and a resonance shaping it — in place.
    auto& L = levelDb;
    const int bins = static_cast<int> (L.size());
    const auto drop = static_cast<float> (cfg.lineDropDb);
    std::fill (lineMask.begin(), lineMask.end(), std::uint8_t { 0 });
    for (int k = std::max (lo, kLineHalfWidth); k <= std::min (hi, bins - 1 - kLineHalfWidth); ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        if (L[i] < L[i - 1] || L[i] < L[i + 1])
            continue;
        // The deepest point within the main lobe on each side (a neighbouring partial may follow).
        float left = L[i], right = L[i];
        for (int d = 1; d <= kLineHalfWidth; ++d)
        {
            left = std::min (left, L[i - static_cast<std::size_t> (d)]);
            right = std::min (right, L[i + static_cast<std::size_t> (d)]);
        }
        if (L[i] - left < drop || L[i] - right < drop)
            continue;
        for (int j = std::max (0, k - kLineHalfWidth); j <= std::min (bins - 1, k + kLineHalfWidth); ++j)
            lineMask[static_cast<std::size_t> (j)] = 1;
    }
    for (int k = 0; k < bins;)
    {
        if (lineMask[static_cast<std::size_t> (k)] == 0)
        {
            ++k;
            continue;
        }
        int end = k;
        while (end + 1 < bins && lineMask[static_cast<std::size_t> (end + 1)] != 0)
            ++end;
        const int left = std::max (0, k - 1), right = std::min (bins - 1, end + 1);
        const float a = L[static_cast<std::size_t> (left)], b = L[static_cast<std::size_t> (right)];
        for (int j = k; j <= end; ++j)
            L[static_cast<std::size_t> (j)] =
                right > left ? a + (b - a) * static_cast<float> (j - left) / static_cast<float> (right - left) : a;
        k = end + 1;
    }
}

void ResonanceDetector::updateFloor (int bins) noexcept
{
    // Minimum over kSubWindows sub-windows of subFrames frames each, plus the current one.
    for (int k = 0; k < bins; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        runningMin[i] = std::min (runningMin[i], levelDb[i]);
        float floor = runningMin[i];
        for (const auto& m : subMins)
            floor = std::min (floor, m[i]);
        floorDb[i] = floor;
    }
    if (++framesInSub >= subFrames)
    {
        subMins[static_cast<std::size_t> (subIndex)] = runningMin;
        subIndex = (subIndex + 1) % kSubWindows;
        std::fill (runningMin.begin(), runningMin.end(), 1.0e30f);
        framesInSub = 0;
    }

    // Light smoothing (±1 bin, in power) against the fine structure of the floor.
    shapeDb.front() = floorDb.front();
    shapeDb[static_cast<std::size_t> (bins - 1)] = floorDb[static_cast<std::size_t> (bins - 1)];
    for (int k = 1; k + 1 < bins; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        const double p = std::pow (10.0, 0.1 * floorDb[i - 1]) + std::pow (10.0, 0.1 * floorDb[i]) + std::pow (10.0, 0.1 * floorDb[i + 1]);
        shapeDb[i] = static_cast<float> (10.0 * std::log10 (p / 3.0 + 1.0e-30));
    }
}

void ResonanceDetector::computeEnvelope (int lo, int hi) noexcept
{
    const int numBins = static_cast<int> (shapeDb.size());
    const double halfWidth = std::pow (2.0, 0.5 * kEnvelopeOctaves);
    for (std::size_t g = 0; g < gridBin.size(); ++g)
    {
        const int centre = gridBin[g];
        const int from = std::max (0, std::min (centre - kMinEnvelopeBins, static_cast<int> (std::floor (centre / halfWidth))));
        const int to = std::min (numBins - 1, std::max (centre + kMinEnvelopeBins, static_cast<int> (std::ceil (centre * halfWidth))));
        const int count = to - from + 1;
        std::copy (shapeDb.begin() + from, shapeDb.begin() + to + 1, window.begin());
        const auto middle = window.begin() + count / 2;
        std::nth_element (window.begin(), middle, window.begin() + count);
        gridDb[g] = *middle;
    }

    // Interpolated per bin along log frequency.
    const double firstHz = gridBin.empty() ? 1.0 : std::max (1, gridBin.front()) * binHz;
    for (int k = lo; k <= hi; ++k)
    {
        const double position = std::log2 (std::max (k, 1) * binHz / firstHz) / kGridStepOctaves;
        const auto g0 = static_cast<std::size_t> (std::clamp (position, 0.0, static_cast<double> (gridDb.size() - 1)));
        const auto g1 = std::min (g0 + 1, gridDb.size() - 1);
        const auto t = static_cast<float> (std::clamp (position - static_cast<double> (g0), 0.0, 1.0));
        envelopeDb[static_cast<std::size_t> (k)] = gridDb[g0] + (gridDb[g1] - gridDb[g0]) * t;
    }
}

void ResonanceDetector::findPeaks (int lo, int hi) noexcept
{
    for (auto& p : peaks)
        p = {};
    candidates.clear();
    int count = 0;
    const auto& L = shapeDb;
    const double threshold = cfg.minProminenceDb - kCandidateMargin;
    for (int k = std::max (lo, 2); k <= hi - 2; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        if (! (L[i] > L[i - 1] && L[i] >= L[i + 1]) || L[i] < kFloorDb)
            continue;
        const double prominence = L[i] - envelopeDb[i];
        if (prominence < threshold)
            continue;

        // Parabolic refinement and the −3 dB span.
        const double ym = L[i - 1], y0 = L[i], yp = L[i + 1];
        const double denominator = ym - 2.0 * y0 + yp;
        const double offset = denominator < 0.0 ? std::clamp (0.5 * (ym - yp) / denominator, -0.5, 0.5) : 0.0;
        const double peakDb = y0 - 0.25 * (ym - yp) * offset;
        const double edge = peakDb - 3.0;
        auto crossing = [&] (int direction) {
            const int last = static_cast<int> (L.size()) - 1;
            int j = k;
            while (j + direction >= 0 && j + direction <= last && std::abs (j - k) < 1024
                   && L[static_cast<std::size_t> (j + direction)] > edge)
                j += direction;
            if (j + direction < 0 || j + direction > last)
                return static_cast<double> (j); // the spectrum ends before the level falls by 3 dB
            const double inside = L[static_cast<std::size_t> (j)];
            const double outside = L[static_cast<std::size_t> (j + direction)];
            const double fraction = inside > outside ? (inside - edge) / (inside - outside) : 0.0;
            return j + direction * fraction;
        };
        const double low = crossing (-1), high = crossing (1);
        const double hz = (k + offset) * binHz;
        const double peakProminence = peakDb - envelopeDb[i];
        if (candidates.size() < candidates.capacity())
            candidates.push_back ({ static_cast<float> (hz), static_cast<float> (peakProminence), static_cast<float> (high - low) });
        const double widthHz = (high - low) * binHz;
        if (widthHz > cfg.maxWidthErb * erbHz (hz) || widthHz < std::min (cfg.minWidthHz, cfg.minWidthErb * erbHz (hz)))
            continue;

        // Audible: not far below the mean level of the ⅓ octave around it (the music masks it otherwise;
        // per bin, so that the test does not depend on the FFT size).
        const double halfBand = std::pow (2.0, 0.5 * kEnvelopeOctaves);
        const auto from = static_cast<std::size_t> (std::max (0, static_cast<int> (std::floor (k / halfBand))));
        const auto to = static_cast<std::size_t> (std::min (static_cast<int> (L.size()) - 1, static_cast<int> (std::ceil (k * halfBand))));
        const double meanDb =
            10.0 * std::log10 ((cumulativePower[to + 1] - cumulativePower[from]) / static_cast<double> (to - from + 1) + 1.0e-30);
        if (peakDb < meanDb - cfg.audibleRangeDb)
            continue;

        const Peak peak { hz, peakProminence, false, low * binHz, high * binHz };
        if (count < kMaxPeaks)
            peaks[static_cast<std::size_t> (count++)] = peak;
        else
        {
            auto weakest = std::min_element (peaks.begin(), peaks.end(),
                                             [] (const Peak& a, const Peak& b) { return a.prominenceDb < b.prominenceDb; });
            if (weakest->prominenceDb < peakProminence)
                *weakest = peak;
        }
    }
    std::sort (peaks.begin(), peaks.begin() + count, [] (const Peak& a, const Peak& b) { return a.prominenceDb > b.prominenceDb; });

    // One candidate per resonance: weaker maxima inside a stronger one's −3 dB span or 1/24 octave go.
    int kept = 0;
    for (int p = 0; p < count; ++p)
    {
        const auto peak = peaks[static_cast<std::size_t> (p)];
        bool covered = false;
        for (int q = 0; q < kept && ! covered; ++q)
        {
            const auto& stronger = peaks[static_cast<std::size_t> (q)];
            covered =
                (peak.hz >= stronger.lowHz && peak.hz <= stronger.highHz) || std::abs (std::log2 (peak.hz / stronger.hz)) < kMatchOctaves;
        }
        if (! covered)
            peaks[static_cast<std::size_t> (kept++)] = peak;
    }
    for (int p = kept; p < kMaxPeaks; ++p)
        peaks[static_cast<std::size_t> (p)].hz = 0.0;
}

double ResonanceDetector::scoreOf (const Track& t) const noexcept
{
    const double prominence = (t.prominenceDb - cfg.minProminenceDb) / 6.0;
    const double persistence = std::min (t.presence, std::min (1.0, t.age));
    const double stability = 1.0 - std::min (1.0, t.drift / (1.0 / 48.0));
    const double ear = std::clamp (sensitivityDb (t.hz) / 10.0, -1.0, 1.0);
    return prominence + 1.5 * persistence + 0.5 * stability - 2.0 * t.harmonicity + 0.3 * ear;
}

void ResonanceDetector::push (const float* binPower, int numBins, const PitchTracker::Result& pitch, double dt) noexcept
{
    const int bins = std::min (numBins, static_cast<int> (levelDb.size()));
    cumulativePower[0] = 0.0;
    for (int k = 0; k < bins; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        levelDb[i] = static_cast<float> (10.0 * std::log10 (static_cast<double> (binPower[k]) + 1.0e-20));
        cumulativePower[i + 1] = cumulativePower[i] + static_cast<double> (binPower[k]);
    }
    const int lo = std::max (2, static_cast<int> (std::ceil (cfg.minHz / binHz)));
    const int hi = std::min (bins - 3, static_cast<int> (std::floor (cfg.maxHz / binHz)));
    if (hi <= lo)
        return;

    removeLines (0, bins - 1);
    updateFloor (bins);
    computeEnvelope (lo, hi);
    findPeaks (lo, hi);

    // A clearly voiced note marks its harmonics (sustained notes that survive the line removal).
    const bool clearPitch = pitch.f0Hz > 0.0 && pitch.aperiodicity < 0.1;
    auto harmonicOf = [&] (double hz) {
        if (! clearPitch)
            return false;
        const double h = std::round (hz / pitch.f0Hz);
        return h >= 1.0 && h <= 16.0 && std::abs (hz - h * pitch.f0Hz) < std::min (0.02 * hz, 0.1 * pitch.f0Hz);
    };
    const double aFast = smoothing (dt, 0.3), aSlow = smoothing (dt, 0.6), aHarmonic = smoothing (dt, clearPitch ? 0.5 : 3.0);

    // Match peaks (most prominent first) to the nearest track within ±1/24 octave.
    for (int t = 0; t < numTracks; ++t)
        tracks[static_cast<std::size_t> (t)].missing += dt; // reset below when matched
    for (auto& peak : peaks)
    {
        if (peak.hz <= 0.0)
            break;
        int best = -1;
        double bestDistance = kMatchOctaves;
        for (int t = 0; t < numTracks; ++t)
        {
            auto& track = tracks[static_cast<std::size_t> (t)];
            if (track.missing <= 0.0)
                continue; // already matched in this frame
            const double distance = std::abs (std::log2 (peak.hz / track.hz));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = t;
            }
        }
        if (best >= 0)
        {
            auto& track = tracks[static_cast<std::size_t> (best)];
            track.drift = aFast * track.drift + (1.0 - aFast) * bestDistance;
            track.hz *= std::pow (2.0, (1.0 - aFast) * std::log2 (peak.hz / track.hz));
            track.prominenceDb = aFast * track.prominenceDb + (1.0 - aFast) * peak.prominenceDb;
            track.presence = aSlow * track.presence + (1.0 - aSlow);
            track.harmonicity = aHarmonic * track.harmonicity + (1.0 - aHarmonic) * (harmonicOf (peak.hz) ? 1.0 : 0.0);
            track.missing = 0.0;
            peak.used = true;
        }
    }

    // Unmatched tracks fade; new tracks for unmatched peaks.
    for (int t = 0; t < numTracks; ++t)
    {
        auto& track = tracks[static_cast<std::size_t> (t)];
        if (track.missing > 0.0)
        {
            track.presence *= aSlow;
            track.prominenceDb = aFast * track.prominenceDb + (1.0 - aFast) * (cfg.minProminenceDb - kCandidateMargin);
        }
        track.age += dt;
    }
    for (const auto& peak : peaks)
    {
        if (peak.hz <= 0.0)
            break;
        if (peak.used)
            continue;
        Track fresh;
        fresh.hz = peak.hz;
        fresh.prominenceDb = peak.prominenceDb;
        fresh.presence = 1.0 - aSlow;
        fresh.harmonicity = harmonicOf (peak.hz) ? 1.0 : 0.0;
        if (numTracks < static_cast<int> (tracks.size()))
            tracks[static_cast<std::size_t> (numTracks++)] = fresh;
        else
        {
            // Replace the weakest hidden track.
            int weakest = -1;
            for (int t = 0; t < numTracks; ++t)
            {
                const auto& track = tracks[static_cast<std::size_t> (t)];
                if (! track.visible && (weakest < 0 || track.score < tracks[static_cast<std::size_t> (weakest)].score))
                    weakest = t;
            }
            if (weakest >= 0)
                tracks[static_cast<std::size_t> (weakest)] = fresh;
        }
    }

    // Score and hysteresis; drop tracks that are gone.
    visibleHints.clear();
    for (int t = 0; t < numTracks;)
    {
        auto& track = tracks[static_cast<std::size_t> (t)];
        track.score = scoreOf (track);
        if (! track.visible)
        {
            track.above = track.score > cfg.showScore ? track.above + dt : 0.0;
            if (track.above >= cfg.showAfterSeconds)
            {
                track.visible = true;
                track.below = 0.0;
                ++changes;
                ++shown;
            }
        }
        else
        {
            track.below = track.score < cfg.hideScore ? track.below + dt : 0.0;
            if (track.below >= cfg.hideAfterSeconds)
            {
                track.visible = false;
                track.above = 0.0;
                ++changes;
            }
        }

        const bool gone = ! track.visible && track.missing > 0.4;
        if (gone)
        {
            tracks[static_cast<std::size_t> (t)] = tracks[static_cast<std::size_t> (numTracks - 1)];
            --numTracks;
            continue;
        }
        if (track.visible)
            visibleHints.push_back ({ static_cast<float> (track.hz), static_cast<float> (track.prominenceDb),
                                      static_cast<float> (track.age), static_cast<float> (std::clamp (track.score / 3.0, 0.0, 1.0)) });
        ++t;
    }
    std::sort (visibleHints.begin(), visibleHints.end(), [] (const Hint& a, const Hint& b) { return a.hz < b.hz; });
}
} // namespace ssc::analysis
