#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ssc
{
/**
    Lock-free triple buffer for publishing snapshots from one writer thread to one reader thread.

    The writer fills writeBuffer() and calls publish(); the reader calls update() and, if it
    returns true, reads the newest snapshot through read(). Neither side ever blocks, and the
    reader always sees a complete snapshot.
*/
template <typename T>
class TripleBuffer
{
public:
    /** Writer side: the buffer to fill before calling publish(). */
    T& writeBuffer() noexcept { return buffers[writeIndex]; }

    /** Writer side: makes the filled buffer available to the reader. */
    void publish() noexcept
    {
        const auto previous = state.exchange (static_cast<std::uint8_t> (writeIndex | kDirtyBit), std::memory_order_acq_rel);
        writeIndex = previous & kIndexMask;
    }

    /** Reader side: returns true if a new snapshot became available since the last call. */
    bool update() noexcept
    {
        if ((state.load (std::memory_order_acquire) & kDirtyBit) == 0)
            return false;

        const auto previous = state.exchange (readIndex, std::memory_order_acq_rel);
        readIndex = previous & kIndexMask;
        return true;
    }

    /** Reader side: the most recent snapshot obtained through update(). */
    const T& read() const noexcept { return buffers[readIndex]; }

private:
    static constexpr std::uint8_t kDirtyBit = 0x4;
    static constexpr std::uint8_t kIndexMask = 0x3;

    std::array<T, 3> buffers {};
    std::uint8_t writeIndex = 0;
    std::uint8_t readIndex = 2;
    std::atomic<std::uint8_t> state { 1 };
};
} // namespace ssc
