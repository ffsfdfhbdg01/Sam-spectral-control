#include "ssc/filters/BandDesign.h"

#include <algorithm>
#include <cmath>

#include "ssc/filters/MatchedDesign.h"
#include "ssc/util/Math.h"

namespace ssc::filters
{
namespace
{
    constexpr double kButterworthQ = 0.70710678118654752;

    struct Builder
    {
        BandDesign design;
        double sampleRate;

        void add (const AnalogSection& a, double matchHz)
        {
            if (design.numSections >= kMaxSections)
                return;
            const auto i = static_cast<std::size_t> (design.numSections++);
            design.analog[i] = a;
            design.digital[i] = matchSection (a, sampleRate, matchHz);
            design.svf[i] = toSvf (design.digital[i]);
        }

        void addDigital (const AnalogSection& a, const Biquad& bq)
        {
            if (design.numSections >= kMaxSections)
                return;
            const auto i = static_cast<std::size_t> (design.numSections++);
            design.analog[i] = a;
            design.digital[i] = bq;
            design.svf[i] = toSvf (bq);
        }
    };

    /**
        Replaces sections [first, end) by their reciprocals. Cuts are designed as the inverse of the
        matching boost: the prototypes used here satisfy H(−G) = 1/H(+G), and inverting the matched boost
        keeps its accuracy (a direct fit of a deep cut can be infeasible).
    */
    void invertSections (BandDesign& d, int first)
    {
        for (int i = first; i < d.numSections; ++i)
        {
            const auto idx = static_cast<std::size_t> (i);
            auto& a = d.analog[idx];
            std::swap (a.n0, a.d0);
            std::swap (a.n1, a.d1);
            std::swap (a.n2, a.d2);

            auto& q = d.digital[idx];
            const double b0 = q.b0;
            const Biquad inv { 1.0 / b0, q.a1 / b0, q.a2 / b0, q.b1 / b0, q.b2 / b0, q.firstOrder };
            q = inv;
            d.svf[idx] = toSvf (q);
        }
    }

    /**
        Damping (1/Q) of the m-th second-order section of an order-N Butterworth cascade:
        2·sin((2m − 1)π / 2N), valid for even and odd N; m = 1 is the highest-Q section.
    */
    double butterworthDamping (int order, int m)
    {
        return 2.0 * std::sin ((2.0 * m - 1.0) * kPi / (2.0 * order));
    }

    /** Butterworth cascade (low-pass or high-pass). The highest-Q section takes the user's resonance. */
    void addCut (Builder& b, double hz, double q, int order, bool highPass)
    {
        const double w = kTwoPi * hz;
        const double resonance = q / kButterworthQ;

        if (order % 2 == 1)
        {
            AnalogSection a;
            a.d1 = 1.0;
            a.d0 = w;
            if (highPass)
            {
                a.n1 = 1.0;
                a.n0 = 0.0;
            }
            else
            {
                a.n0 = w;
            }
            b.add (a, hz);
        }

        for (int m = 1; m <= order / 2; ++m)
        {
            double damping = butterworthDamping (order, m);
            if (m == 1)
                damping /= resonance; // the highest-Q section carries the user's resonance
            AnalogSection a;
            a.d2 = 1.0;
            a.d1 = w * damping;
            a.d0 = w * w;
            if (highPass)
            {
                a.n2 = 1.0;
                a.n0 = 0.0;
            }
            else
            {
                a.n0 = w * w;
            }
            b.add (a, hz);
        }
    }

    /**
        Butterworth-type shelving filter of order M (Holters & Zölzer 2006):
        |H|² = (Ω^2M + g·ω0^2M) / (Ω^2M + ω0^2M / g) for the low shelf, so the gain at ω0 is half the dB gain.
        The highest-Q section takes the user's resonance (bump); for M = 2 this equals the RBJ shelf.
    */
    void addShelf (Builder& b, double hz, double gainDb, double q, int order, bool high)
    {
        // The matched fit needs the section's poles inside the band. For a low shelf those are the
        // poles of the boost, for a high shelf the poles of the cut; the other polarity is the inverse.
        const int first = b.design.numSections;
        const double g = dbToGain (std::abs (gainDb));
        const double w = kTwoPi * hz;
        const double spread = std::pow (g, 1.0 / (2.0 * order));
        const double wLow = w / spread;
        const double wHigh = w * spread;
        const double resonance = std::clamp (q, 0.3, 4.0) / kButterworthQ;

        // Low shelf boost: zeros at wHigh, poles at wLow. High shelf cut: zeros at wHigh, poles at wLow,
        // per-section gain 1/g^(1/M) so that the cut reaches 1/g at high frequencies.
        const bool designedIsInverse = high ? gainDb > 0.0 : gainDb < 0.0;

        if (order % 2 == 1)
        {
            AnalogSection a;
            a.n1 = high ? std::pow (g, -1.0 / order) : 1.0;
            a.n0 = a.n1 * wHigh;
            if (high)
                a.n0 = a.n1 * wHigh;
            a.d1 = 1.0;
            a.d0 = wLow;
            b.add (a, hz);
        }

        for (int m = 1; m <= order / 2; ++m)
        {
            double damping = butterworthDamping (order, m);
            if (m == 1)
                damping /= resonance;
            const double scale = high ? std::pow (g, -2.0 / order) : 1.0;
            AnalogSection a;
            a.n2 = scale;
            a.n1 = scale * damping * wHigh;
            a.n0 = scale * wHigh * wHigh;
            a.d2 = 1.0;
            a.d1 = damping * wLow;
            a.d0 = wLow * wLow;
            b.add (a, hz);
        }

        if (designedIsInverse)
            invertSections (b.design, first);
    }

    void addBell (Builder& b, double hz, double gainDb, double q)
    {
        const int first = b.design.numSections;
        const double A = std::pow (10.0, std::abs (gainDb) / 40.0);
        const double w = kTwoPi * hz;
        AnalogSection a;
        a.n2 = 1.0;
        a.n1 = A * w / q;
        a.n0 = w * w;
        a.d2 = 1.0;
        a.d1 = w / (A * q);
        a.d0 = w * w;
        b.add (a, hz);
        if (gainDb < 0.0)
            invertSections (b.design, first);
    }

    void addBandPass (Builder& b, double hz, double q, int sections)
    {
        const double w = kTwoPi * hz;
        for (int i = 0; i < sections; ++i)
        {
            AnalogSection a;
            a.n1 = w / q;
            a.n0 = 0.0;
            a.d2 = 1.0;
            a.d1 = w / q;
            a.d0 = w * w;
            b.add (a, hz);
        }
    }

    void addNotch (Builder& b, double hz, double q)
    {
        const double w = kTwoPi * hz;
        AnalogSection a;
        a.n2 = 1.0;
        a.n0 = w * w;
        a.d2 = 1.0;
        a.d1 = w / q;
        a.d0 = w * w;
        b.addDigital (a, matchedNotch (hz, q, b.sampleRate));
    }

    void addAllPass (Builder& b, double hz, double q)
    {
        const double w = kTwoPi * hz;
        AnalogSection a;
        a.n2 = 1.0;
        a.n1 = -w / q;
        a.n0 = w * w;
        a.d2 = 1.0;
        a.d1 = w / q;
        a.d0 = w * w;
        b.addDigital (a, matchedAllPass (hz, q, b.sampleRate));
    }

    /** Tilt: lows at −gain, highs at +gain, pivot at hz. */
    void addTilt (Builder& b, double hz, double gainDb, double q, int order)
    {
        const int first = b.design.numSections;
        addShelf (b, hz, 2.0 * gainDb, q, order, true);
        if (b.design.numSections > first)
        {
            const double scale = dbToGain (-gainDb);
            const auto i = static_cast<std::size_t> (first);
            auto& a = b.design.analog[i];
            a.n0 *= scale;
            a.n1 *= scale;
            a.n2 *= scale;
            auto& d = b.design.digital[i];
            d.b0 *= scale;
            d.b1 *= scale;
            d.b2 *= scale;
            b.design.svf[i] = toSvf (d);
        }
    }

    /**
        Flat tilt: a constant dB/oct slope over the audible range, pivoting at hz. The gain is the
        total tilt between 20 Hz and 20 kHz; five first-order shelves two octaves apart approximate it.
    */
    void addFlatTilt (Builder& b, double hz, double gainDb)
    {
        const double slopePerOctave = gainDb / std::log2 (1000.0);
        const double sectionGain = 2.0 * slopePerOctave;
        double pivotCorrectionDb = 0.0;
        for (int i = 0; i < 5; ++i)
        {
            const double centre = 40.0 * std::pow (4.0, i);
            addShelf (b, centre, sectionGain, kButterworthQ, 1, true);
            // Level of this shelf at the pivot, to re-centre the whole curve there.
            const auto& a = b.design.analog[static_cast<std::size_t> (b.design.numSections - 1)];
            pivotCorrectionDb += 10.0 * std::log10 (a.magnitudeSquared (kTwoPi * hz));
        }

        const double scale = dbToGain (-pivotCorrectionDb);
        auto& a = b.design.analog[0];
        a.n0 *= scale;
        a.n1 *= scale;
        a.n2 *= scale;
        auto& d = b.design.digital[0];
        d.b0 *= scale;
        d.b1 *= scale;
        d.b2 *= scale;
        b.design.svf[0] = toSvf (d);
    }

    int bandPassSections (Slope slope)
    {
        switch (slope)
        {
            case Slope::Db6: return 1;
            case Slope::Db12: return 2;
            case Slope::Db18: return 3;
            case Slope::Db24: return 4;
            case Slope::Db30: return 5;
            case Slope::Db36: return 6;
            default: return 8;
        }
    }
} // namespace

int slopeOrder (Slope slope) noexcept
{
    return slope == Slope::Brickwall ? 16 : slopeDbPerOctave (slope) / 6;
}

bool isIdentity (const BandShape& s) noexcept
{
    if (! shapeHasGain (s.shape))
        return false;
    return std::abs (s.gainDb) < 1.0e-9;
}

BandDesign designBand (const BandShape& s, double sampleRate)
{
    Builder b { {}, sampleRate };
    const double hz = std::clamp (s.frequencyHz, 1.0, 0.5 * sampleRate * 1.5);
    const double q = std::clamp (s.q, 0.01, 100.0);

    if (isIdentity (s))
        return b.design;

    switch (s.shape)
    {
        case Shape::Bell: addBell (b, hz, s.gainDb, q); break;
        case Shape::LowShelf: addShelf (b, hz, s.gainDb, q, std::clamp (slopeOrder (s.slope), 1, 8), false); break;
        case Shape::HighShelf: addShelf (b, hz, s.gainDb, q, std::clamp (slopeOrder (s.slope), 1, 8), true); break;
        case Shape::LowCut: addCut (b, hz, q, slopeOrder (s.slope), true); break;
        case Shape::HighCut: addCut (b, hz, q, slopeOrder (s.slope), false); break;
        case Shape::Notch: addNotch (b, hz, q); break;
        case Shape::BandPass: addBandPass (b, hz, q, bandPassSections (s.slope)); break;
        case Shape::TiltShelf: addTilt (b, hz, s.gainDb, q, s.slope == Slope::Db6 ? 1 : 2); break;
        case Shape::FlatTilt: addFlatTilt (b, hz, s.gainDb); break;
        case Shape::AllPass: addAllPass (b, hz, q); break;
    }

    return b.design;
}

std::complex<double> BandDesign::analogResponse (double hz) const noexcept
{
    std::complex<double> h (1.0, 0.0);
    const double omega = kTwoPi * hz;
    for (int i = 0; i < numSections; ++i)
        h *= analog[static_cast<std::size_t> (i)].response (omega);
    return h;
}

std::complex<double> BandDesign::digitalResponse (double hz, double sampleRate) const noexcept
{
    std::complex<double> h (1.0, 0.0);
    const double omega = kTwoPi * hz / sampleRate;
    for (int i = 0; i < numSections; ++i)
        h *= digital[static_cast<std::size_t> (i)].response (omega);
    return h;
}
} // namespace ssc::filters
