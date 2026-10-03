#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <vector>

#include "ssc/util/Math.h"

namespace ssc
{
/**
    Lock-free single-producer / single-consumer ring buffer.

    The producer (audio thread) never waits: when the buffer is full, the samples that do not
    fit are dropped and write() reports how many were stored. Memory is allocated only in the
    constructor or in allocate(), never in read() or write().
*/
template <typename T>
class SpscRingBuffer
{
public:
    SpscRingBuffer() = default;

    explicit SpscRingBuffer (std::size_t minimumCapacity) { allocate (minimumCapacity); }

    /** Not thread-safe; call before the producer and consumer start. */
    void allocate (std::size_t minimumCapacity)
    {
        std::size_t capacity = 1;
        while (capacity < minimumCapacity)
            capacity <<= 1;

        storage.assign (capacity, T {});
        mask = capacity - 1;
        writePos.store (0, std::memory_order_relaxed);
        readPos.store (0, std::memory_order_relaxed);
    }

    std::size_t capacity() const noexcept { return storage.size(); }

    /** Producer side. Returns the number of items actually written. */
    std::size_t write (const T* data, std::size_t count) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto r = readPos.load (std::memory_order_acquire);
        const auto freeSpace = storage.size() - (w - r);
        const auto n = count < freeSpace ? count : freeSpace;

        for (std::size_t i = 0; i < n; ++i)
            storage[(w + i) & mask] = data[i];

        writePos.store (w + n, std::memory_order_release);
        return n;
    }

    /** Consumer side. Returns the number of items actually read. */
    std::size_t read (T* destination, std::size_t count) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);
        const auto w = writePos.load (std::memory_order_acquire);
        const auto available = w - r;
        const auto n = count < available ? count : available;

        for (std::size_t i = 0; i < n; ++i)
            destination[i] = storage[(r + i) & mask];

        readPos.store (r + n, std::memory_order_release);
        return n;
    }

    std::size_t availableToRead() const noexcept
    {
        return writePos.load (std::memory_order_acquire) - readPos.load (std::memory_order_relaxed);
    }

    std::size_t availableToWrite() const noexcept
    {
        return storage.size() - (writePos.load (std::memory_order_relaxed) - readPos.load (std::memory_order_acquire));
    }

    /** Consumer side: discards everything currently readable. */
    void discardAll() noexcept { readPos.store (writePos.load (std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<T> storage;
    std::size_t mask = 0;
    alignas (64) std::atomic<std::size_t> writePos { 0 };
    alignas (64) std::atomic<std::size_t> readPos { 0 };
};
} // namespace ssc
