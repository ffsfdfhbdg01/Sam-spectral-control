#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ssc
{
inline constexpr double kPi = std::numbers::pi;
inline constexpr double kTwoPi = 2.0 * std::numbers::pi;

/** Lowest level reported by conversions to dB, so silence never produces -inf. */
inline constexpr double kMinDb = -200.0;

template <typename T>
inline T dbToGain (T db) noexcept
{
    return std::pow (T (10), db / T (20));
}

template <typename T>
inline T gainToDb (T gain, T floorDb = T (kMinDb)) noexcept
{
    return gain > T (0) ? std::max (T (20) * std::log10 (gain), floorDb) : floorDb;
}

template <typename T>
inline T powerToDb (T power, T floorDb = T (kMinDb)) noexcept
{
    return power > T (0) ? std::max (T (10) * std::log10 (power), floorDb) : floorDb;
}

constexpr bool isPowerOfTwo (int n) noexcept
{
    return n > 0 && (n & (n - 1)) == 0;
}

constexpr int nextPowerOfTwo (int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace ssc
