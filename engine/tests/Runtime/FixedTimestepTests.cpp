#include <doctest/doctest.h>

#include "Runtime/FixedTimestep.h"

#include <stdexcept>

using zodar::FixedTimestep;
using zodar::FixedTimestepDesc;

TEST_CASE("FixedTimestep accumulates fractional frames into whole steps") {
    FixedTimestep step({0.01, 8, 0.25});
    CHECK(step.advance(0.004) == 0);
    CHECK(step.alpha() == doctest::Approx(0.4));
    CHECK(step.advance(0.004) == 0);
    CHECK(step.advance(0.004) == 1); // 0.012 -> one step, 0.002 left
    CHECK(step.alpha() == doctest::Approx(0.2));
    CHECK(step.totalSteps() == 1);
    CHECK(step.simulationTime() == doctest::Approx(0.01));
}

TEST_CASE("FixedTimestep runs exact multiples without losing a step to rounding") {
    FixedTimestep step({1.0 / 60.0});
    CHECK(step.advance(2.0 / 60.0) == 2);
    uint32_t total = 0;
    for (int i = 0; i < 600; ++i) total += step.advance(1.0 / 60.0);
    CHECK(total == 600);
    CHECK(step.alpha() < 1e-6);
}

TEST_CASE("FixedTimestep clamps long frames and caps steps per frame") {
    FixedTimestep step({0.01, 4, 0.25});
    CHECK(step.advance(10.0) == 4);           // clamped to 0.25 s, capped at 4 steps
    CHECK(step.alpha() >= 0.0);
    CHECK(step.alpha() < 1.0);
    CHECK(step.droppedTime() == doctest::Approx(10.0 - 0.04).epsilon(1e-6));
    CHECK(step.advance(0.0) == 0);            // nothing carried over beyond one step
    CHECK(step.advance(-1.0) == 0);           // negative frame time is ignored
}

TEST_CASE("FixedTimestep is deterministic for identical input") {
    FixedTimestep a({1.0 / 60.0});
    FixedTimestep b({1.0 / 60.0});
    const double frames[] = {0.016, 0.033, 0.001, 0.05, 0.0167, 0.2};
    for (double f : frames) CHECK(a.advance(f) == b.advance(f));
    CHECK(a.totalSteps() == b.totalSteps());
    CHECK(a.alpha() == b.alpha());
}

TEST_CASE("FixedTimestep rejects invalid settings") {
    CHECK_THROWS(FixedTimestep({0.0}));
    CHECK_THROWS(FixedTimestep({0.01, 0}));
    CHECK_THROWS(FixedTimestep({0.01, 4, 0.0}));
    CHECK_THROWS(FixedTimestep({0.5, 4, 0.25})); // maxFrameTime < fixedDt would never step
}
