#pragma once

#include <algorithm>
#include <cmath>

namespace ssc
{
/**
    Linear ramp towards a target value over a fixed number of samples.
    When the ramp ends the value snaps exactly to the target, so identity settings
    (gain 1, mix 1) are reached bit-exactly.
*/
template <typename T>
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, static_cast<int> (std::lround (sampleRate * rampSeconds)));
    }

    void setCurrentAndTarget (T value) noexcept
    {
        current = target = value;
        remaining = 0;
    }

    void setTarget (T value) noexcept
    {
        if (value == target)
            return;

        target = value;
        remaining = rampLength;
        step = (target - current) / static_cast<T> (remaining);
    }

    T next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0)
                current = target;
        }
        return current;
    }

    void skip (int numSamples) noexcept
    {
        if (remaining <= 0)
            return;

        if (numSamples >= remaining)
        {
            current = target;
            remaining = 0;
        }
        else
        {
            current += step * static_cast<T> (numSamples);
            remaining -= numSamples;
        }
    }

    bool isSmoothing() const noexcept { return remaining > 0; }
    T getCurrent() const noexcept { return current; }
    T getTarget() const noexcept { return target; }

private:
    T current {}, target {}, step {};
    int remaining = 0;
    int rampLength = 1;
};
} // namespace ssc
