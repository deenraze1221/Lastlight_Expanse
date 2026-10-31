#include <doctest/doctest.h>

#include "Core/Profiler.h"

#include <thread>

using namespace zodar;

TEST_CASE("cpu profiler: nested scopes and frame totals") {
    CpuProfiler p;
    p.beginFrame();
    {
        ZODAR_CPU_SCOPE(p, "Outer");
        {
            ZODAR_CPU_SCOPE(p, "Inner");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    p.endFrame();
    REQUIRE(p.lastFrame().size() == 2);
    CHECK(p.lastFrame()[0].name == "Outer");
    CHECK(p.lastFrame()[1].depth == 1);
    CHECK(p.lastFrame()[1].ms >= 1.5);
    CHECK(p.lastFrame()[0].ms >= p.lastFrame()[1].ms);
    CHECK(p.lastFrameMs() >= p.lastFrame()[0].ms);
}

TEST_CASE("history: ring buffer statistics") {
    History h(4);
    for (float v : {1.0f, 2.0f, 3.0f, 4.0f, 10.0f}) h.push(v); // 1 drops out
    CHECK(h.count() == 4);
    CHECK(h.average() == doctest::Approx(4.75f));
    CHECK(h.max() == 10.0f);
    CHECK(h.percentile(0.0f) == 2.0f);
    CHECK(h.percentile(1.0f) == 10.0f);
}
