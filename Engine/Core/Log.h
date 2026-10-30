#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace raven::log {

struct Config {
    spdlog::level::level_enum level = spdlog::level::info;
    // Optional log file. Empty means console only.
    std::string filePath;
};

// Creates the engine logger. Safe to call more than once; later calls replace the sinks.
void init(const Config& config = {});
void shutdown();
bool isInitialized();

// Engine logger. Falls back to a console logger if init() was never called.
spdlog::logger& engine();

} // namespace raven::log

#define RAVEN_LOG_TRACE(...) ::raven::log::engine().trace(__VA_ARGS__)
#define RAVEN_LOG_DEBUG(...) ::raven::log::engine().debug(__VA_ARGS__)
#define RAVEN_LOG_INFO(...)  ::raven::log::engine().info(__VA_ARGS__)
#define RAVEN_LOG_WARN(...)  ::raven::log::engine().warn(__VA_ARGS__)
#define RAVEN_LOG_ERROR(...) ::raven::log::engine().error(__VA_ARGS__)
