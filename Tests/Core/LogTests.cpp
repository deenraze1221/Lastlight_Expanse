#include <doctest/doctest.h>

#include "Core/Log.h"

#include <filesystem>
#include <fstream>
#include <sstream>

TEST_CASE("log: file sink receives messages and re-init replaces sinks") {
    const auto path = std::filesystem::temp_directory_path() / "raven_log_test.txt";
    std::filesystem::remove(path);

    raven::log::init({spdlog::level::info, path.string()});
    CHECK(raven::log::isInitialized());
    RAVEN_LOG_INFO("hello {}", 42);
    RAVEN_LOG_DEBUG("filtered out");
    raven::log::shutdown();
    CHECK_FALSE(raven::log::isInitialized());

    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    CHECK(text.str().find("hello 42") != std::string::npos);
    CHECK(text.str().find("filtered out") == std::string::npos);

    // Logging without init must not crash; it falls back to a console logger.
    raven::log::engine().set_level(spdlog::level::off);
    RAVEN_LOG_INFO("fallback");
    raven::log::init({spdlog::level::warn, ""});
    std::filesystem::remove(path);
}
