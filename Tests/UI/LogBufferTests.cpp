#include <doctest/doctest.h>

#include "UI/LogBuffer.h"

using namespace zodar::ui;

TEST_CASE("ui log buffer: keeps the newest lines up to capacity") {
    LogBuffer log(3);
    for (int i = 0; i < 5; ++i) log.add({double(i), LogLevel::Info, "line " + std::to_string(i)});
    REQUIRE(log.lines().size() == 3);
    CHECK(log.lines().front().text == "line 2");
    CHECK(log.lines().back().text == "line 4");
    CHECK(log.dropped() == 2);
    log.clear();
    CHECK(log.lines().empty());
    CHECK(LogBuffer(0).capacity() == 1);
}

TEST_CASE("ui log buffer: line formatting") {
    CHECK(formatLogLine({3.5, LogLevel::Error, "boom"}) == "[003.50] boom");
    CHECK(formatLogLine({-1.0, LogLevel::Info, "x"}) == "[000.00] x");
}
