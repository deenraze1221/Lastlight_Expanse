#include <doctest/doctest.h>

#include "Core/Error.h"

#include <string>

TEST_CASE("error: fail throws raven::Error with source location") {
    try {
        raven::fail("boom");
        FAIL("fail() returned");
    } catch (const raven::Error& e) {
        const std::string message = e.what();
        CHECK(message.find("boom") != std::string::npos);
        CHECK(message.find("ErrorTests.cpp") != std::string::npos);
    }
}

TEST_CASE("error: RAVEN_ENSURE passes and fails") {
    CHECK_NOTHROW(RAVEN_ENSURE(1 + 1 == 2, "math"));
    CHECK_THROWS_AS(RAVEN_ENSURE(1 + 1 == 3, "math"), raven::Error);
}
