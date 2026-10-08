#include "ssc/analysis/BandReadouts.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ssc/dynamics/Dynamics.h"
#include "ssc/spectral/SpectralEngine.h"
#include "ssc/util/Math.h"

namespace ssc::analysis
{
namespace
{
    constexpr double kImpulse = 1.0e-3;      ///< small, so that nonlinear Characters stay linear
    constexpr double kPreRollSeconds = 0.25; ///< smoothing, crossfades and corrector updates settle
    constexpr double kMaxSeconds = 5.0;      ///< longest response followed (research §12c)
    constexpr double kTailDb = -90.0;        ///< the response is over when it stays this far down …
    constexpr double kTailSeconds = 0.05;    ///< … for this long
    constexpr int kBlock = 4096;
    constexpr double kActiveDb = 0.5; ///< the band acts where |gain| ≥ 0.5 dB

    double dbOf (double magnitude) noexcept
    {
        return 20.0 * std::log10 (std::max (magnitude, 1.0e-30));
    }

    /** Envelope crossing between two peaks (index, |h|), interpolated in dB. */
    double crossing (std::size_t a, double va, std::size_t b, double vb, double threshold) noexcept
    {
        const double da = dbOf (va), db = dbOf (vb), dt = dbOf (threshold);
        if (std::abs (da - db) < 1.0e-12)
            return static_cast<double> (a);
        return static_cast<double> (a) + (static_cast<double> (b) - static_cast<double> (a)) * (da - dt) / (da - db);
    }
} // namespace

RingTimes ringTimes (const std::vector<double>& h, double thresholdDb, std::size_t zeroIndex)
{
    RingTimes r;
    if (h.empty())
        return r;
    const std::size_t n = h.size();
    for (std::size_t i = 1; i < n; ++i)
        if (std::abs (h[i]) > std::abs (h[r.peak]))
            r.peak = i;
    const double threshold = std::abs (h[r.peak]) * std::pow (10.0, -thresholdDb / 20.0);
    auto isPeak = [&] (std::size_t i) {
        const double v = std::abs (h[i]);
        return (i == 0 || v >= std::abs (h[i - 1])) && (i + 1 == n || v > std::abs (h[i + 1]));
    };

    // After the peak: the last local maximum above the threshold, then the envelope crossing
    // towards the next (lower) maximum.
    std::size_t last = r.peak, next = n;
    for (std::size_t i = r.peak + 1; i < n; ++i)
    {
        if (! isPeak (i))
            continue;
        if (std::abs (h[i]) >= threshold)
        {
            last = i;
            next = n;
        }
        else if (next == n)
            next = i;
    }
    const double end = last == r.peak ? static_cast<double> (r.peak)
        : next < n                    ? crossing (last, std::abs (h[last]), next, std::abs (h[next]), threshold)
                                      : static_cast<double> (last);
    r.post = std::max (0.0, end - static_cast<double> (r.peak));

    // Before the zero time: the first local maximum above the threshold, and the crossing from
    // the (lower) maximum before it.
    const std::size_t zero = std::min (r.peak, zeroIndex);
    std::size_t first = zero, previous = n;
    for (std::size_t i = 0; i < zero; ++i)
    {
        if (! isPeak (i))
            continue;
        if (std::abs (h[i]) >= threshold)
        {
            first = i;
            break;
        }
        previous = i;
    }
    const double start = first == zero ? static_cast<double> (zero)
        : previous < n                 ? crossing (previous, std::abs (h[previous]), first, std::abs (h[first]), threshold)
                                       : static_cast<double> (first);
    r.pre = std::max (0.0, static_cast<double> (zero) - start);
    return r;
}

void BandReadouts::prepare (double sampleRate)
{
    fs = sampleRate;
    engine = std::make_unique<engine::Engine>();
    engine::EngineConfig config;
    config.sampleRate = fs;
    config.maxBlockSize = kBlock;
    config.numChannels = 1;
    config.topology = engine::ChannelTopology::mono();
    engine->prepare (config);
    chainEngine.reset();
}

void BandReadouts::runImpulse (engine::Engine& e, int numChannels, const engine::BandSettingsArray& bands,
                               const engine::GlobalParams& params)
{
    auto p = params;
    p.autoGain = false;
    p.mix = 1.0;
    p.outputGainDb = 0.0;
    p.bypass = false;
    p.delta = false;
    p.soloBand = -1;
    p.maxLookaheadMs = 0.0;
    p.nonRealtime = false;

    e.reset();
    std::array<std::vector<double>, 2> blocks;
    for (auto& b : blocks)
        b.assign (static_cast<std::size_t> (kBlock), 0.0);
    double* channels[] = { blocks[0].data(), blocks[1].data() };
    auto clear = [&blocks] {
        for (auto& b : blocks)
            std::fill (b.begin(), b.end(), 0.0);
    };
    for (int done = 0; done < static_cast<int> (kPreRollSeconds * fs); done += kBlock)
    {
        if (aborted())
            return;
        clear();
        e.process (channels, numChannels, kBlock, p, bands);
    }
    latency = e.getLatencySamples();
    const auto& block = blocks[0];

    response.clear();
    const auto maxLength = static_cast<std::size_t> (latency + kMaxSeconds * fs);
    const auto tail = static_cast<std::size_t> (kTailSeconds * fs);
    double peak = 0.0;
    std::size_t lastLoud = 0;
    bool first = true;
    while (response.size() < maxLength)
    {
        if (aborted())
            return;
        clear();
        if (first)
            for (auto& b : blocks)
                b.front() = kImpulse;
        first = false;
        e.process (channels, numChannels, kBlock, p, bands);
        for (const double v : block)
        {
            const double x = v / kImpulse;
            response.push_back (x);
            peak = std::max (peak, std::abs (x));
            if (std::abs (x) > peak * std::pow (10.0, kTailDb / 20.0))
                lastLoud = response.size();
        }
        if (response.size() > static_cast<std::size_t> (latency) + static_cast<std::size_t> (kBlock) && response.size() > lastLoud + tail)
            break;
    }
    response.resize (std::min (response.size(), std::max (lastLoud + tail, static_cast<std::size_t> (latency) + 1)));
}

int BandReadouts::responseSpectra (int minimumSize)
{
    const int n = std::clamp (nextPowerOfTwo (std::max (static_cast<int> (response.size()) * 2, minimumSize)), 16384, 1 << 20);
    if (fft.size() != n)
        fft.prepare (n);
    buffer.assign (static_cast<std::size_t> (n), 0.0f);
    spectrum.assign (static_cast<std::size_t> (n + 2), 0.0f);
    rampSpectrum.assign (static_cast<std::size_t> (n + 2), 0.0f);
    const auto length = std::min (response.size(), static_cast<std::size_t> (n));
    for (std::size_t i = 0; i < length; ++i)
        buffer[i] = static_cast<float> (response[i]);
    fft.forward (buffer.data(), spectrum.data());
    for (std::size_t i = 0; i < length; ++i)
        buffer[i] = static_cast<float> (response[i] * static_cast<double> (i));
    fft.forward (buffer.data(), rampSpectrum.data());
    return n;
}

BandReadouts::Measurement BandReadouts::measure (const engine::BandSettings& band, const engine::GlobalParams& params)
{
    engine::BandSettingsArray bands {};
    auto& b = bands[0];
    b = band;
    b.used = b.active = true;
    b.mode = BandMode::Static;
    b.component = StereoComponent::All;
    b.channelMask = ~std::uint64_t { 0 };
    b.includeLfe = true;
    runImpulse (*engine, 1, bands, params);
    Measurement m;
    if (aborted())
        return m;

    // Ringing.
    const auto rings = ringTimes (response, 60.0, static_cast<std::size_t> (latency));
    m.preRingMs = static_cast<float> (1000.0 * rings.pre / fs);
    m.postRingMs = static_cast<float> (1000.0 * rings.post / fs);

    // Spectrum of h and of n·h (group delay).
    const int n = responseSpectra();

    const int bins = n / 2;
    auto at = [&] (const std::vector<float>& s, int k) {
        return std::complex<double> (s[static_cast<std::size_t> (2 * k)], s[static_cast<std::size_t> (2 * k + 1)]);
    };
    auto binOf = [&] (double hz) { return std::clamp (static_cast<int> (std::lround (hz / fs * n)), 1, bins - 1); };

    // Group delay at fc (excess: the engine latency removed), from the nearest well-conditioned bin.
    {
        const int centre = binOf (std::min (band.shape.frequencyHz, 0.45 * fs));
        for (int d = 0; d < 64; ++d)
        {
            const int k = d % 2 == 0 ? centre + d / 2 : centre - (d + 1) / 2;
            if (k < 1 || k >= bins)
                continue;
            const auto x = at (spectrum, k);
            if (std::norm (x) < 1.0e-12)
                continue;
            const double tau = (at (rampSpectrum, k) * std::conj (x)).real() / std::norm (x);
            m.groupDelayMs = static_cast<float> (1000.0 * (tau - latency) / fs);
            break;
        }
    }

    // Excess phase, unwrapped from the bin where the band does least, on a log grid.
    std::vector<double> phase (static_cast<std::size_t> (bins), 0.0);
    int reference = binOf (20.0);
    double smallest = 1.0e9;
    for (int k = 1; k < bins; ++k)
    {
        const auto x = at (spectrum, k) * std::polar (1.0, kTwoPi * k * latency / n);
        phase[static_cast<std::size_t> (k)] = std::arg (x);
        const double hz = static_cast<double> (k) * fs / n;
        if (hz >= 20.0 && hz <= 0.45 * fs && std::abs (dbOf (std::abs (x))) < smallest)
        {
            smallest = std::abs (dbOf (std::abs (x)));
            reference = k;
        }
    }
    for (int k = reference + 1; k < bins; ++k)
    {
        auto& p = phase[static_cast<std::size_t> (k)];
        p += kTwoPi * std::round ((phase[static_cast<std::size_t> (k - 1)] - p) / kTwoPi);
    }
    for (int k = reference - 1; k >= 1; --k)
    {
        auto& p = phase[static_cast<std::size_t> (k)];
        p += kTwoPi * std::round ((phase[static_cast<std::size_t> (k + 1)] - p) / kTwoPi);
    }
    double weighted = 0.0, weights = 0.0;
    const double top = std::min (20000.0, 0.45 * fs);
    for (int g = 0; g < 512; ++g)
    {
        const double hz = 20.0 * std::pow (top / 20.0, g / 511.0);
        const int k = binOf (hz);
        const double gainDb = dbOf (std::abs (at (spectrum, k)));
        if (std::abs (gainDb) < kActiveDb)
            continue;
        weighted += std::abs (gainDb) * std::abs (phase[static_cast<std::size_t> (k)]);
        weights += std::abs (gainDb);
    }
    m.meanPhaseDeg = weights > 0.0 ? static_cast<float> (weighted / weights * 180.0 / kPi) : 0.0f;
    return m;
}

ChainResponse BandReadouts::computeChain (const engine::BandSettingsArray& bands, const engine::GlobalParams& params,
                                          const std::array<float, kNumBands>& dynamicGainsDb)
{
    ChainResponse r;
    if (! engine)
        return r;
    if (! chainEngine)
    {
        chainEngine = std::make_unique<engine::Engine>();
        engine::EngineConfig config;
        config.sampleRate = fs;
        config.maxBlockSize = kBlock;
        config.numChannels = 2;
        config.topology = engine::ChannelTopology::stereo();
        chainEngine->prepare (config);
    }

    // The bands as heard now: Dynamic ones at their current gain, Spectral ones (zero-phase) out.
    engine::BandSettingsArray chain = bands;
    for (std::size_t b = 0; b < chain.size(); ++b)
    {
        auto& band = chain[b];
        if (band.isSpectral())
            band.active = false;
        if (band.mode == BandMode::Dynamic)
        {
            if (shapeHasGain (band.shape.shape))
                band.shape.gainDb += dynamicGainsDb[b];
            band.mode = BandMode::Static;
        }
        band.channelMask = ~std::uint64_t { 0 };
        band.includeLfe = true;
    }
    runImpulse (*chainEngine, 2, chain, params);
    if (aborted())
        return r;

    // Zero-padded to ≤ 0.25 Hz per bin: finer than the log grid at 10 Hz.
    const int n = responseSpectra (static_cast<int> (std::ceil (fs / 0.25)));
    const int bins = n / 2;
    auto at = [&] (const std::vector<float>& s, int k) {
        return std::complex<double> (s[static_cast<std::size_t> (2 * k)], s[static_cast<std::size_t> (2 * k + 1)]);
    };
    const double top = std::min (30000.0, 0.49 * fs);
    r.numPoints = ChainResponse::kMaxPoints;
    for (int i = 0; i < r.numPoints; ++i)
    {
        const auto p = static_cast<std::size_t> (i);
        const double hz = 10.0 * std::pow (top / 10.0, i / static_cast<double> (r.numPoints - 1));
        const int k = std::clamp (static_cast<int> (std::lround (hz / fs * n)), 1, bins - 1);
        const auto h = at (spectrum, k);
        const auto excess = h * std::polar (1.0, kTwoPi * k * latency / n);
        r.hz[p] = static_cast<float> (hz);
        r.phaseDeg[p] = static_cast<float> (std::arg (excess) * 180.0 / kPi);
        r.defined[p] = std::abs (h) >= 1.0e-3;
        if (r.defined[p])
        {
            const double tau = (at (rampSpectrum, k) * std::conj (h)).real() / std::norm (h);
            r.groupDelayMs[p] = static_cast<float> (1000.0 * (tau - latency) / fs);
        }
    }
    r.valid = true;
    return r;
}

BandReadout BandReadouts::compute (const engine::BandSettings& band, const engine::GlobalParams& params, double dynamicGainDb)
{
    BandReadout r;
    if (! engine)
        return r;

    if (band.isSpectral())
    {
        // The STFT mask is zero-phase; its time smearing is about half a frame each way.
        const double half = 0.5 * spectral::fftSizeFor (params.quality, fs) / fs * 1000.0;
        r.stftPreEchoMs = static_cast<float> (half);
        r.preRingMs = r.postRingMs = static_cast<float> (half);
        r.valid = true;
        return r;
    }

    const bool dynamic = band.mode == BandMode::Dynamic && shapeHasGain (band.shape.shape);
    auto current = band;
    if (dynamic)
        current.shape.gainDb += dynamicGainDb;
    const auto now = measure (current, params);
    if (aborted())
        return {};
    r.meanPhaseDeg = now.meanPhaseDeg;
    r.groupDelayMs = now.groupDelayMs;
    r.preRingMs = now.preRingMs;
    r.postRingMs = now.postRingMs;

    if (dynamic)
    {
        // The full range: the gain change far past the threshold, in the band's direction.
        const auto& d = band.dynamics;
        const auto computer = dynamics::GainComputer::make (d.thresholdDb, d.ratio, d.kneeDb, d.rangeDb, d.direction);
        auto full = band;
        full.shape.gainDb += d.mix * computer (d.direction == Direction::Above ? 300.0 : -300.0);
        const auto worst = measure (full, params);
        if (aborted())
            return {};
        r.rangePreRingMs = worst.preRingMs;
        r.rangePostRingMs = worst.postRingMs;
    }
    r.valid = true;
    return r;
}
} // namespace ssc::analysis
