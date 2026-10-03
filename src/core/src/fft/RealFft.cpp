#include "ssc/fft/RealFft.h"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "pffft.h"
#include "ssc/util/Math.h"

namespace ssc
{
RealFft::~RealFft()
{
    release();
}

RealFft::RealFft (RealFft&& other) noexcept
    : setup (std::exchange (other.setup, nullptr)),
      buffer (std::exchange (other.buffer, nullptr)),
      work (std::exchange (other.work, nullptr)),
      n (std::exchange (other.n, 0))
{
}

RealFft& RealFft::operator= (RealFft&& other) noexcept
{
    if (this != &other)
    {
        release();
        setup = std::exchange (other.setup, nullptr);
        buffer = std::exchange (other.buffer, nullptr);
        work = std::exchange (other.work, nullptr);
        n = std::exchange (other.n, 0);
    }
    return *this;
}

void RealFft::release() noexcept
{
    if (setup != nullptr)
        pffft_destroy_setup (setup);
    if (buffer != nullptr)
        pffft_aligned_free (buffer);
    if (work != nullptr)
        pffft_aligned_free (work);

    setup = nullptr;
    buffer = nullptr;
    work = nullptr;
    n = 0;
}

void RealFft::prepare (int size)
{
    if (! isPowerOfTwo (size) || size < 32)
        throw std::invalid_argument ("RealFft size must be a power of two >= 32");

    if (size == n)
        return;

    release();
    setup = pffft_new_setup (size, PFFFT_REAL);
    if (setup == nullptr)
        throw std::runtime_error ("pffft_new_setup failed");

    const auto bytes = static_cast<std::size_t> (size) * sizeof (float);
    buffer = static_cast<float*> (pffft_aligned_malloc (bytes));
    work = static_cast<float*> (pffft_aligned_malloc (bytes));
    n = size;
}

void RealFft::forward (const float* time, float* spectrum) noexcept
{
    assert (setup != nullptr);
    std::memcpy (buffer, time, static_cast<std::size_t> (n) * sizeof (float));
    pffft_transform_ordered (setup, buffer, buffer, work, PFFFT_FORWARD);

    // pffft packs DC in [0] and Nyquist in [1]; unpack to the N + 2 interleaved layout.
    const float dc = buffer[0];
    const float nyquist = buffer[1];
    std::memcpy (spectrum + 2, buffer + 2, static_cast<std::size_t> (n - 2) * sizeof (float));
    spectrum[0] = dc;
    spectrum[1] = 0.0f;
    spectrum[n] = nyquist;
    spectrum[n + 1] = 0.0f;
}

void RealFft::inverse (const float* spectrum, float* time) noexcept
{
    assert (setup != nullptr);
    buffer[0] = spectrum[0];
    buffer[1] = spectrum[n];
    std::memcpy (buffer + 2, spectrum + 2, static_cast<std::size_t> (n - 2) * sizeof (float));
    pffft_transform_ordered (setup, buffer, buffer, work, PFFFT_BACKWARD);

    const float scale = 1.0f / static_cast<float> (n);
    for (int i = 0; i < n; ++i)
        time[i] = buffer[i] * scale;
}
} // namespace ssc
