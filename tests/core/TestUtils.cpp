#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "ssc/util/DelayLine.h"
#include "ssc/util/LinearSmoother.h"
#include "ssc/util/Math.h"

using Catch::Approx;

TEST_CASE ("dB conversions are inverse and floored", "[util]")
{
    REQUIRE (ssc::dbToGain (0.0) == 1.0);
    REQUIRE (ssc::dbToGain (-6.0) == Approx (0.501187).epsilon (1e-5));
    REQUIRE (ssc::gainToDb (ssc::dbToGain (-12.5)) == Approx (-12.5));
    REQUIRE (ssc::gainToDb (0.0) == ssc::kMinDb);
    REQUIRE (ssc::powerToDb (0.01) == Approx (-20.0));
}

TEST_CASE ("power of two helpers", "[util]")
{
    REQUIRE (ssc::isPowerOfTwo (1024));
    REQUIRE_FALSE (ssc::isPowerOfTwo (1000));
    REQUIRE (ssc::nextPowerOfTwo (1000) == 1024);
    REQUIRE (ssc::nextPowerOfTwo (1) == 1);
}

TEST_CASE ("LinearSmoother ramps linearly and snaps exactly to target", "[util]")
{
    ssc::LinearSmoother<double> s;
    s.prepare (1000.0, 0.01); // 10 samples
    s.setCurrentAndTarget (0.0);
    s.setTarget (1.0);

    double last = 0.0;
    for (int i = 0; i < 9; ++i)
    {
        const double v = s.next();
        REQUIRE (v > last);
        last = v;
    }
    REQUIRE (s.isSmoothing());
    REQUIRE (s.next() == 1.0); // exact, not approximately
    REQUIRE_FALSE (s.isSmoothing());
    REQUIRE (s.next() == 1.0);
}

TEST_CASE ("LinearSmoother skip lands on the same value as stepping", "[util]")
{
    ssc::LinearSmoother<double> a, b;
    a.prepare (1000.0, 0.1);
    b.prepare (1000.0, 0.1);
    a.setCurrentAndTarget (2.0);
    b.setCurrentAndTarget (2.0);
    a.setTarget (-1.0);
    b.setTarget (-1.0);

    for (int i = 0; i < 37; ++i)
        a.next();
    b.skip (37);
    REQUIRE (a.getCurrent() == Approx (b.getCurrent()).margin (1e-12));

    b.skip (1000);
    REQUIRE (b.getCurrent() == -1.0);
}

TEST_CASE ("MultiChannelDelay delays every channel by the configured amount", "[util]")
{
    ssc::MultiChannelDelay<double> delay;
    delay.prepare (2, 100, 16);
    delay.setDelay (5);

    std::vector<double> left (64), right (64);
    for (int i = 0; i < 64; ++i)
    {
        left[static_cast<std::size_t> (i)] = i + 1;
        right[static_cast<std::size_t> (i)] = -(i + 1);
    }

    std::vector<double> outL, outR;
    for (int block = 0; block < 4; ++block)
    {
        double* ptrs[] = { left.data() + block * 16, right.data() + block * 16 };
        delay.processInPlace (ptrs, 2, 16);
        outL.insert (outL.end(), ptrs[0], ptrs[0] + 16);
        outR.insert (outR.end(), ptrs[1], ptrs[1] + 16);
    }

    for (int i = 0; i < 64; ++i)
    {
        const double expected = i < 5 ? 0.0 : static_cast<double> (i - 5 + 1);
        REQUIRE (outL[static_cast<std::size_t> (i)] == expected);
        REQUIRE (outR[static_cast<std::size_t> (i)] == -expected);
    }
}

TEST_CASE ("MultiChannelDelay with zero delay is a bit-exact pass-through", "[util]")
{
    ssc::MultiChannelDelay<double> delay;
    delay.prepare (1, 10, 8);
    std::vector<double> data { 0.1, -0.2, 0.3, 1e-30, -7.0, 0.0, 2.5, 9.0 };
    const auto copy = data;
    double* ptrs[] = { data.data() };
    delay.processInPlace (ptrs, 1, 8);
    REQUIRE (data == copy);
}
