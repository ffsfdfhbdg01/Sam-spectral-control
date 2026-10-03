#pragma once

#include "ssc/filters/Sections.h"

namespace ssc::filters
{
struct SectionState
{
    double s1 = 0.0;
    double s2 = 0.0;
};

/** One sample through a trapezoidal (TPT) state-variable section; see SvfParams. */
inline double processSection (const SvfParams& p, bool firstOrder, SectionState& st, double x) noexcept
{
    if (firstOrder)
    {
        const double G = p.g / (1.0 + p.g);
        const double v = (x - st.s1) * G;
        const double lp = v + st.s1;
        st.s1 = lp + v;
        return p.m0 * x + p.m1 * lp;
    }

    const double c1 = 1.0 / (1.0 + p.g * (p.g + p.k));
    const double c2 = p.g * c1;
    const double c3 = p.g * c2;
    const double v3 = x - st.s2;
    const double v1 = c1 * st.s1 + c2 * v3;
    const double v2 = st.s2 + c2 * st.s1 + c3 * v3;
    st.s1 = 2.0 * v1 - st.s1;
    st.s2 = 2.0 * v2 - st.s2;
    return p.m0 * x + p.m1 * v1 + p.m2 * v2;
}

inline SvfParams lerp (const SvfParams& a, const SvfParams& b, double t) noexcept
{
    return { a.g + (b.g - a.g) * t, a.k + (b.k - a.k) * t, a.m0 + (b.m0 - a.m0) * t, a.m1 + (b.m1 - a.m1) * t, a.m2 + (b.m2 - a.m2) * t };
}
} // namespace ssc::filters
