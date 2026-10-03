#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "ssc/util/SpscRingBuffer.h"

TEST_CASE ("SpscRingBuffer rounds capacity up to a power of two", "[util][spsc]")
{
    ssc::SpscRingBuffer<float> rb (1000);
    REQUIRE (rb.capacity() == 1024);
    REQUIRE (rb.availableToRead() == 0);
    REQUIRE (rb.availableToWrite() == 1024);
}

TEST_CASE ("SpscRingBuffer preserves order and drops what does not fit", "[util][spsc]")
{
    ssc::SpscRingBuffer<int> rb (8);
    std::vector<int> in { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    REQUIRE (rb.write (in.data(), in.size()) == 8); // two items dropped, producer never blocks
    REQUIRE (rb.availableToWrite() == 0);

    std::vector<int> out (5);
    REQUIRE (rb.read (out.data(), 5) == 5);
    REQUIRE (out == std::vector<int> { 1, 2, 3, 4, 5 });

    REQUIRE (rb.write (in.data(), 4) == 4);
    std::vector<int> rest (10);
    REQUIRE (rb.read (rest.data(), 10) == 7);
    rest.resize (7);
    REQUIRE (rest == std::vector<int> { 6, 7, 8, 1, 2, 3, 4 });
}

TEST_CASE ("SpscRingBuffer discardAll empties the readable region", "[util][spsc]")
{
    ssc::SpscRingBuffer<int> rb (16);
    int data[5] = { 1, 2, 3, 4, 5 };
    rb.write (data, 5);
    rb.discardAll();
    REQUIRE (rb.availableToRead() == 0);
    REQUIRE (rb.availableToWrite() == 16);
}

TEST_CASE ("SpscRingBuffer transfers a long sequence between threads without corruption", "[util][spsc][threads]")
{
    constexpr int total = 2'000'000;
    ssc::SpscRingBuffer<int> rb (1 << 12);

    std::thread producer ([&rb] {
        int next = 0;
        int chunk[97];
        while (next < total)
        {
            int count = 0;
            while (count < 97 && next + count < total)
            {
                chunk[count] = next + count;
                ++count;
            }
            std::size_t written = 0;
            while (written < static_cast<std::size_t> (count))
                written += rb.write (chunk + written, static_cast<std::size_t> (count) - written);
            next += count;
        }
    });

    int expected = 0;
    bool ordered = true;
    int buffer[61];
    while (expected < total)
    {
        const auto n = rb.read (buffer, 61);
        for (std::size_t i = 0; i < n; ++i)
            ordered = ordered && (buffer[i] == expected++);
    }
    producer.join();

    REQUIRE (ordered);
    REQUIRE (expected == total);
}
