#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "ssc/util/TripleBuffer.h"

namespace
{
struct Snapshot
{
    long a = 0;
    long b = 0; // always equal to a in a consistent snapshot
};
} // namespace

TEST_CASE ("TripleBuffer delivers the newest published snapshot", "[util][triple]")
{
    ssc::TripleBuffer<int> tb;
    REQUIRE_FALSE (tb.update());

    tb.writeBuffer() = 1;
    tb.publish();
    tb.writeBuffer() = 2;
    tb.publish();

    REQUIRE (tb.update());
    REQUIRE (tb.read() == 2);
    REQUIRE_FALSE (tb.update()); // nothing new
    REQUIRE (tb.read() == 2);

    tb.writeBuffer() = 3;
    tb.publish();
    REQUIRE (tb.update());
    REQUIRE (tb.read() == 3);
}

TEST_CASE ("TripleBuffer never exposes torn or stale-reordered snapshots", "[util][triple][threads]")
{
    ssc::TripleBuffer<Snapshot> tb;
    std::atomic<bool> done { false };

    std::thread writer ([&] {
        for (long i = 1; i <= 500'000; ++i)
        {
            auto& s = tb.writeBuffer();
            s.a = i;
            s.b = i;
            tb.publish();
        }
        done = true;
    });

    long last = 0;
    bool consistent = true;
    bool monotonic = true;
    auto check = [&] {
        tb.update();
        const auto& s = tb.read();
        consistent = consistent && s.a == s.b;
        monotonic = monotonic && s.a >= last;
        last = s.a;
    };

    while (! done.load())
        check();
    writer.join();
    check();

    REQUIRE (consistent);
    REQUIRE (monotonic);
    REQUIRE (last == 500'000);
}
