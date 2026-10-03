#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

namespace ssc
{
/**
    Multi-channel integer delay used to align dry signals with the processed path.
    All memory is allocated in prepare(); process() is real-time safe.
*/
template <typename T>
class MultiChannelDelay
{
public:
    void prepare (int numChannels, int maxDelaySamples, int maxBlockSize)
    {
        std::size_t size = 1;
        while (size < static_cast<std::size_t> (maxDelaySamples + maxBlockSize + 1))
            size <<= 1;

        lines.assign (static_cast<std::size_t> (numChannels), std::vector<T> (size, T {}));
        mask = size - 1;
        maxDelay = maxDelaySamples;
        writePos = 0;
        delay = 0;
    }

    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), T {});
        writePos = 0;
    }

    void setDelay (int samples) noexcept
    {
        assert (samples >= 0 && samples <= maxDelay);
        delay = samples < 0 ? 0 : (samples > maxDelay ? maxDelay : samples);
    }

    int getDelay() const noexcept { return delay; }

    /**
        Delays numSamples of every channel in place. All channels must be processed
        with the same numSamples in each call (the write position is shared).
    */
    void processInPlace (T* const* channels, int numChannels, int numSamples) noexcept
    {
        if (lines.empty())
            return;

        const auto n = static_cast<std::size_t> (numSamples);
        const auto d = static_cast<std::size_t> (delay);

        for (int ch = 0; ch < numChannels && ch < static_cast<int> (lines.size()); ++ch)
        {
            auto& line = lines[static_cast<std::size_t> (ch)];
            auto* data = channels[ch];

            for (std::size_t i = 0; i < n; ++i)
            {
                line[(writePos + i) & mask] = data[i];
                data[i] = line[(writePos + i - d) & mask];
            }
        }

        writePos = (writePos + n) & mask;
    }

private:
    std::vector<std::vector<T>> lines;
    std::size_t mask = 0;
    std::size_t writePos = 0;
    int maxDelay = 0;
    int delay = 0;
};
} // namespace ssc
