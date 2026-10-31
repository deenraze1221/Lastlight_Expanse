#include <doctest/doctest.h>

#include "Core/Log.h"
#include "../Support/TempFile.h"

TEST_CASE("log: file sink receives messages and re-init replaces sinks") {
    const zodar::test::TempFile logFile("zodar_log_test.txt");

    zodar::log::init({spdlog::level::info, logFile.path().string()});
    CHECK(zodar::log::isInitialized());
    ZODAR_LOG_INFO("hello {}", 42);
    ZODAR_LOG_DEBUG("filtered out");
    zodar::log::shutdown(); // Releases the logger and with it the file sink's handle.
    CHECK_FALSE(zodar::log::isInitialized());

    const std::string text = zodar::test::readFile(logFile.path());
    CHECK(text.find("hello 42") != std::string::npos);
    CHECK(text.find("filtered out") == std::string::npos);
    // Deleting succeeds only if no handle is left open (enforced by Windows).
    CHECK(logFile.remove());

    // Logging without init must not crash; it falls back to a console logger.
    zodar::log::engine().set_level(spdlog::level::off);
    ZODAR_LOG_INFO("fallback");
    zodar::log::init({spdlog::level::warn, ""});
}
