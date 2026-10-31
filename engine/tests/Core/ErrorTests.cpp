#include <doctest/doctest.h>

#include "Core/Error.h"

#include <string>

TEST_CASE("error: fail throws zodar::Error with source location") {
    try {
        zodar::fail("boom");
        FAIL("fail() returned");
    } catch (const zodar::Error& e) {
        const std::string message = e.what();
        CHECK(message.find("boom") != std::string::npos);
        CHECK(message.find("ErrorTests.cpp") != std::string::npos);
    }
}

TEST_CASE("error: ZODAR_ENSURE passes and fails") {
    CHECK_NOTHROW(ZODAR_ENSURE(1 + 1 == 2, "math"));
    CHECK_THROWS_AS(ZODAR_ENSURE(1 + 1 == 3, "math"), zodar::Error);
}
