#include "AllocationCounter.h"

#include <cstdlib>
#include <new>

namespace
{
thread_local std::size_t allocations = 0;
thread_local int counters = 0;

void* allocate (std::size_t size)
{
    if (counters > 0)
        ++allocations;
    if (void* p = std::malloc (size > 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void* allocateNoThrow (std::size_t size) noexcept
{
    if (counters > 0)
        ++allocations;
    return std::malloc (size > 0 ? size : 1);
}
} // namespace

// The aligned forms keep their default implementations (a matching pair of their own).
void* operator new (std::size_t size)
{
    return allocate (size);
}

void* operator new[] (std::size_t size)
{
    return allocate (size);
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    return allocateNoThrow (size);
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    return allocateNoThrow (size);
}

void operator delete (void* p) noexcept
{
    std::free (p);
}

void operator delete[] (void* p) noexcept
{
    std::free (p);
}

void operator delete (void* p, std::size_t) noexcept
{
    std::free (p);
}

void operator delete[] (void* p, std::size_t) noexcept
{
    std::free (p);
}

void operator delete (void* p, const std::nothrow_t&) noexcept
{
    std::free (p);
}

void operator delete[] (void* p, const std::nothrow_t&) noexcept
{
    std::free (p);
}

namespace ssc::test
{
ScopedAllocationCounter::ScopedAllocationCounter() noexcept
    : start (allocations)
{
    ++counters;
}

ScopedAllocationCounter::~ScopedAllocationCounter()
{
    --counters;
}

std::size_t ScopedAllocationCounter::count() const noexcept
{
    return allocations - start;
}
} // namespace ssc::test
