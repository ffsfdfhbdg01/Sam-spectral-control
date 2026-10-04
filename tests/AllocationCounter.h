#pragma once

#include <cstddef>

namespace ssc::test
{
/**
    Counts heap allocations made through operator new on the calling thread while an instance
    exists (TST-4: the audio thread must not allocate). AllocationCounter.cpp replaces the global
    operator new and must be linked into the test executable.

    Allocations straight from malloc (C libraries, juce::HeapBlock) are not seen.
*/
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter() noexcept;
    ~ScopedAllocationCounter();

    ScopedAllocationCounter (const ScopedAllocationCounter&) = delete;
    ScopedAllocationCounter& operator= (const ScopedAllocationCounter&) = delete;

    /** Allocations on this thread since the counter was created. */
    std::size_t count() const noexcept;

private:
    std::size_t start = 0;
};
} // namespace ssc::test
