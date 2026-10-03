#include "ssc/oversampling/Oversampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ssc::oversampling
{
namespace
{
    constexpr double kPi = std::numbers::pi;

    // --- Elliptic half-band design (Valenzuela & Constantinides; algorithm as published with HIIR) ---

    void transitionParams (double transition, double& k, double& q) noexcept
    {
        k = std::tan ((1.0 - transition * 2.0) * kPi / 4.0);
        k *= k;
        const double kk = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
        const double e4 = e * e * e * e;
        q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    }

    double accNumerator (double q, int order, int c) noexcept
    {
        double acc = 0.0, term = 0.0;
        int i = 0, sign = 1;
        do
        {
            term = std::pow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * kPi / order) * sign;
            acc += term;
            sign = -sign;
            ++i;
        } while (std::abs (term) > 1e-100 && i < 1000);
        return acc;
    }

    double accDenominator (double q, int order, int c) noexcept
    {
        double acc = 0.0, term = 0.0;
        int i = 1, sign = -1;
        do
        {
            term = std::pow (q, i * i) * std::cos (i * 2 * c * kPi / order) * sign;
            acc += term;
            sign = -sign;
            ++i;
        } while (std::abs (term) > 1e-100 && i < 1000);
        return acc;
    }

    // --- Kaiser window ---

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

    /** Dot product with four partial sums (the same order for every call: deterministic). */
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

    /** Kaiser-windowed half-band FIR of length 4M − 1 for an attenuation target. */
    std::vector<double> kaiserHalfband (double attenuationDb, double transitionWidth)
    {
        // Kaiser's formulas for β and the order; the length is rounded up to 4M − 1 so that the centre
        // tap has an odd index and the polyphase branches split cleanly.
        const double beta = attenuationDb > 50.0 ? 0.1102 * (attenuationDb - 8.7)
                                                 : 0.5842 * std::pow (attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
        const int minLength = static_cast<int> (std::ceil ((attenuationDb - 7.95) / (2.285 * 2.0 * kPi * transitionWidth))) + 1;
        const int length = std::max (7, 4 * ((minLength + 4) / 4) - 1);
        const int centre = (length - 1) / 2;

        std::vector<double> taps (static_cast<std::size_t> (length), 0.0);
        double sideSum = 0.0;
        for (int n = 0; n < length; ++n)
        {
            const int k = n - centre;
            if (k == 0 || k % 2 == 0)
                continue;
            const double r = static_cast<double> (k) / centre;
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            taps[static_cast<std::size_t> (n)] = std::sin (kPi * k / 2.0) / (kPi * k) * window;
            sideSum += taps[static_cast<std::size_t> (n)];
        }
        // The side taps of an exact half-band filter sum to ½: scale them so the DC gain is exactly 1.
        for (auto& t : taps)
            t *= 0.5 / sideSum;
        taps[static_cast<std::size_t> (centre)] = 0.5;
        return taps;
    }

    /** Largest |H(f)| of a symmetric half-band FIR over its stopband [¼ + transition/2, ½]. */
    double stopbandPeak (const std::vector<double>& taps, double transitionWidth) noexcept
    {
        const int length = static_cast<int> (taps.size());
        const int centre = (length - 1) / 2;
        const double start = 0.25 + transitionWidth / 2.0;
        const int points = std::max (64, static_cast<int> (std::ceil ((0.5 - start) * 32.0 * length)));
        double peak = 0.0;
        for (int i = 0; i <= points; ++i)
        {
            const double f = start + (0.5 - start) * i / points;
            double h = taps[static_cast<std::size_t> (centre)];
            for (int k = 1; k <= centre; k += 2)
                h += 2.0 * taps[static_cast<std::size_t> (centre + k)] * std::cos (2.0 * kPi * f * k);
            peak = std::max (peak, std::abs (h));
        }
        return peak;
    }
} // namespace

/** One 2× up/down stage for several channels. */
class Stage
{
public:
    virtual ~Stage() = default;
    virtual void prepare (int numChannels) = 0;
    virtual void reset() noexcept = 0;
    /** in: n samples; out: 2n samples. */
    virtual void upsample (int channel, const double* in, double* out, int n) noexcept = 0;
    /** in: 2n samples; out: n samples. */
    virtual void downsample (int channel, const double* in, double* out, int n) noexcept = 0;
    /** Round-trip (up + down) group delay at low frequencies, in samples of the stage's low rate. */
    virtual double roundTripDelay() const noexcept = 0;
};

namespace
{
    /**
        H(z) = ½·[A₀(z²) + z⁻¹·A₁(z²)], each Aᵢ a chain of first-order allpasses (c + z⁻¹)/(1 + c·z⁻¹)
        running at the low rate. Up: even output = A₀(x), odd output = A₁(x). Down (as in HIIR): the
        odd input sample feeds A₀ and the even one A₁, which takes the later of the two phases.
    */
    class IirStage final : public Stage
    {
    public:
        explicit IirStage (std::vector<double> c)
            : coefs (std::move (c))
        {
        }

        void prepare (int numChannels) override
        {
            state.assign (static_cast<std::size_t> (numChannels), std::vector<double> (coefs.size() * 4, 0.0));
        }

        void reset() noexcept override
        {
            for (auto& memory : state)
                std::fill (memory.begin(), memory.end(), 0.0);
        }

        void upsample (int channel, const double* in, double* out, int n) noexcept override
        {
            double* memory = state[static_cast<std::size_t> (channel)].data();
            for (int i = 0; i < n; ++i)
            {
                double even = in[i], odd = in[i];
                run (memory, even, odd);
                out[2 * i] = even;
                out[2 * i + 1] = odd;
            }
        }

        void downsample (int channel, const double* in, double* out, int n) noexcept override
        {
            double* memory = state[static_cast<std::size_t> (channel)].data() + coefs.size() * 2;
            for (int i = 0; i < n; ++i)
            {
                double branch0 = in[2 * i + 1];
                double branch1 = in[2 * i];
                run (memory, branch0, branch1);
                out[i] = 0.5 * (branch0 + branch1);
            }
        }

        double roundTripDelay() const noexcept override
        {
            // Group delay at DC of (c + z⁻¹)/(1 + c·z⁻¹) is (1 − c)/(1 + c) low-rate samples. H delays by
            // the mean of both branches, (2·B₀ + 1 + 2·B₁)/2 high-rate samples; the round trip is twice
            // that minus the high-rate sample saved by the decimator's phase: B₀ + B₁ low-rate samples.
            double sum = 0.0;
            for (const double c : coefs)
                sum += (1.0 - c) / (1.0 + c);
            return sum;
        }

    private:
        /** Branch 0 runs the even coefficients, branch 1 the odd ones; y = c·(x − y₋₁) + x₋₁. */
        void run (double* memory, double& a, double& b) const noexcept
        {
            const std::size_t n = coefs.size();
            double* xs = memory;
            double* ys = memory + n;
            for (std::size_t c = 0; c < n; c += 2)
            {
                const double x0 = xs[c];
                xs[c] = a;
                a = (a - ys[c]) * coefs[c] + x0;
                ys[c] = a;
                if (c + 1 < n)
                {
                    const double x1 = xs[c + 1];
                    xs[c + 1] = b;
                    b = (b - ys[c + 1]) * coefs[c + 1] + x1;
                    ys[c + 1] = b;
                }
            }
        }

        std::vector<double> coefs;
        std::vector<std::vector<double>> state; ///< per channel: x and y of every allpass, up then down
    };

    /**
        Half-band FIR h of length 4M − 1 (centre c = 2M − 1) in polyphase form. Only the even taps
        h[0], h[2], …, h[4M − 2] are non-zero besides the centre ½:
        up:   out[2n] = 2·Σⱼ h[2j]·x[n − j],  out[2n + 1] = x[n − (M − 1)]
        down: y[n] = Σⱼ h[2j]·v[2(n − j)] + ½·v[2(n − M) + 1]
        Histories are stored twice in a row so every dot product reads contiguous memory.
    */
    class FirStage final : public Stage
    {
    public:
        explicit FirStage (const std::vector<double>& taps)
            : half ((static_cast<int> (taps.size()) + 1) / 4)
        {
            for (int k = static_cast<int> (taps.size()) - 1; k >= 0; k -= 2)
                reversed.push_back (taps[static_cast<std::size_t> (k)]);
        }

        void prepare (int numChannels) override
        {
            const auto size = static_cast<std::size_t> (4 * half);
            channels.assign (
                static_cast<std::size_t> (numChannels),
                Channel { std::vector<double> (size, 0.0), std::vector<double> (size, 0.0), std::vector<double> (size, 0.0), 0, 0 });
        }

        void reset() noexcept override
        {
            for (auto& ch : channels)
            {
                std::fill (ch.up.begin(), ch.up.end(), 0.0);
                std::fill (ch.downEven.begin(), ch.downEven.end(), 0.0);
                std::fill (ch.downOdd.begin(), ch.downOdd.end(), 0.0);
                ch.upPos = ch.downPos = 0;
            }
        }

        void upsample (int channel, const double* in, double* out, int n) noexcept override
        {
            auto& ch = channels[static_cast<std::size_t> (channel)];
            const int taps = 2 * half;
            double* history = ch.up.data();
            int pos = ch.upPos;
            for (int i = 0; i < n; ++i)
            {
                history[pos] = history[pos + taps] = in[i];
                pos = pos + 1 == taps ? 0 : pos + 1;
                const double* window = history + pos; // oldest … newest
                out[2 * i] = 2.0 * dot (window, reversed.data(), taps);
                out[2 * i + 1] = window[half];
            }
            ch.upPos = pos;
        }

        void downsample (int channel, const double* in, double* out, int n) noexcept override
        {
            auto& ch = channels[static_cast<std::size_t> (channel)];
            const int taps = 2 * half;
            double* even = ch.downEven.data();
            double* odd = ch.downOdd.data();
            int pos = ch.downPos;
            for (int i = 0; i < n; ++i)
            {
                even[pos] = even[pos + taps] = in[2 * i];
                odd[pos] = odd[pos + taps] = in[2 * i + 1];
                pos = pos + 1 == taps ? 0 : pos + 1;
                out[i] = dot (even + pos, reversed.data(), taps) + 0.5 * odd[pos + half - 1];
            }
            ch.downPos = pos;
        }

        double roundTripDelay() const noexcept override { return 2 * half - 1; }

    private:
        struct Channel
        {
            std::vector<double> up, downEven, downOdd;
            int upPos, downPos;
        };

        int half = 0;                 ///< M
        std::vector<double> reversed; ///< h[4M − 2], h[4M − 4], …, h[0]
        std::vector<Channel> channels;
    };
} // namespace

FilterSpec filterSpecFor (Quality quality) noexcept
{
    // Normal: 19 kHz at 44.1 kHz inside the passband; Max: 20 kHz (OS-2).
    switch (quality)
    {
        case Quality::Eco: return { 0.420, 80.0, 0.1 };
        case Quality::Normal: return { 0.432, 90.0, 0.05 };
        case Quality::High: return { 0.444, 105.0, 0.02 };
        case Quality::Max: return { 0.454, 120.0, 0.01 };
    }
    return {};
}

std::vector<double> designPolyphaseHalfband (double attenuationDb, double transitionWidth)
{
    double k = 0.0, q = 0.0;
    transitionParams (transitionWidth, k, q);

    const double attenuationPower = std::pow (10.0, -attenuationDb / 10.0);
    const double a = attenuationPower / (1.0 - attenuationPower);
    int order = static_cast<int> (std::ceil (std::log (a * a / 16.0) / std::log (q)));
    if (order % 2 == 0)
        ++order;
    order = std::max (order, 3);
    const int count = (order - 1) / 2;

    std::vector<double> coefs (static_cast<std::size_t> (count));
    for (int index = 0; index < count; ++index)
    {
        const int c = index + 1;
        const double num = accNumerator (q, order, c) * std::pow (q, 0.25);
        const double den = accDenominator (q, order, c) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coefs[static_cast<std::size_t> (index)] = (1.0 - x) / (1.0 + x);
    }
    return coefs;
}

std::vector<double> designHalfbandFir (double attenuationDb, double transitionWidth)
{
    // Kaiser's estimates of β and of the length are slightly optimistic for half-band filters (and
    // for short ones in particular): raise the design target until the measured stopband complies.
    const double limit = 0.98 * std::pow (10.0, -attenuationDb / 20.0);
    for (double target = attenuationDb; target < attenuationDb + 30.0; target += 0.5)
    {
        auto taps = kaiserHalfband (target, transitionWidth);
        if (stopbandPeak (taps, transitionWidth) <= limit)
            return taps;
    }
    return kaiserHalfband (attenuationDb + 30.0, transitionWidth);
}

Oversampler::Oversampler() = default;
Oversampler::~Oversampler() = default;
Oversampler::Oversampler (Oversampler&&) noexcept = default;
Oversampler& Oversampler::operator= (Oversampler&&) noexcept = default;

void Oversampler::prepare (int numChannels, int maxBlockSize, int newFactor, FilterType type, Quality quality)
{
    channels = std::max (1, numChannels);
    maxBlock = std::max (1, maxBlockSize);
    factor = newFactor >= 8 ? 8 : (newFactor >= 4 ? 4 : (newFactor >= 2 ? 2 : 1));
    filterType = type;

    stages.clear();
    const auto spec = filterSpecFor (quality);
    double delay = 0.0;
    double rate = 1.0; // low rate of the stage relative to the base rate
    for (int f = 2; f <= factor; f *= 2)
    {
        // Every stage protects the passband of the base rate: later stages have a wider transition.
        const double edge = spec.passband / (2.0 * rate); // as a fraction of this stage's high rate
        const double transition = std::min (0.45, 0.5 - 2.0 * edge);
        std::unique_ptr<Stage> stage;
        if (type == FilterType::PolyphaseIir)
            stage = std::make_unique<IirStage> (designPolyphaseHalfband (spec.attenuationDb, transition));
        else
            stage = std::make_unique<FirStage> (designHalfbandFir (spec.attenuationDb, transition));
        stage->prepare (channels);
        delay += stage->roundTripDelay() / rate;
        stages.push_back (std::move (stage));
        rate *= 2.0;
    }

    // Complete the round trip to a whole number of base-rate samples.
    filterDelay = delay;
    latency = static_cast<int> (std::ceil (delay - 1.0e-9));
    padding = static_cast<int> (std::lround ((latency - delay) * factor));
    paddingLines.assign (static_cast<std::size_t> (channels), std::vector<double> (static_cast<std::size_t> (std::max (1, padding)), 0.0));
    paddingPos.assign (static_cast<std::size_t> (channels), 0);

    work.assign (static_cast<std::size_t> (channels), std::vector<double> (static_cast<std::size_t> (maxBlock * factor), 0.0));
}

void Oversampler::reset() noexcept
{
    for (auto& s : stages)
        s->reset();
    for (auto& line : paddingLines)
        std::fill (line.begin(), line.end(), 0.0);
    std::fill (paddingPos.begin(), paddingPos.end(), 0);
}

void Oversampler::upsample (int channel, const double* in, double* out, int numSamples) noexcept
{
    if (stages.empty())
    {
        std::copy (in, in + numSamples, out);
        return;
    }

    // Alternate between the scratch buffer and `out` so that the last stage writes into `out`.
    double* scratch = work[static_cast<std::size_t> (channel)].data();
    const double* source = in;
    int n = numSamples;
    const std::size_t count = stages.size();
    for (std::size_t s = 0; s < count; ++s)
    {
        double* target = (count - s) % 2 == 1 ? out : scratch;
        stages[s]->upsample (channel, source, target, n);
        source = target;
        n *= 2;
    }
}

void Oversampler::downsample (int channel, double* in, double* out, int numSamples) noexcept
{
    const int highSamples = numSamples * factor;
    if (stages.empty())
    {
        std::copy (in, in + numSamples, out);
        return;
    }

    if (padding > 0)
    {
        auto& line = paddingLines[static_cast<std::size_t> (channel)];
        int pos = paddingPos[static_cast<std::size_t> (channel)];
        for (int i = 0; i < highSamples; ++i)
        {
            const double delayed = line[static_cast<std::size_t> (pos)];
            line[static_cast<std::size_t> (pos)] = in[i];
            in[i] = delayed;
            pos = pos + 1 == padding ? 0 : pos + 1;
        }
        paddingPos[static_cast<std::size_t> (channel)] = pos;
    }

    double* scratch = work[static_cast<std::size_t> (channel)].data();
    double* source = in;
    int n = highSamples;
    for (std::size_t s = stages.size(); s-- > 0;)
    {
        n /= 2;
        double* target = s == 0 ? out : (source == in ? scratch : in);
        stages[s]->downsample (channel, source, target, n);
        source = target;
    }
}
} // namespace ssc::oversampling
