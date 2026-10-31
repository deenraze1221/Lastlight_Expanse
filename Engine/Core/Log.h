#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace zodar::log {

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

} // namespace zodar::log

#define ZODAR_LOG_TRACE(...) ::zodar::log::engine().trace(__VA_ARGS__)
#define ZODAR_LOG_DEBUG(...) ::zodar::log::engine().debug(__VA_ARGS__)
#define ZODAR_LOG_INFO(...)  ::zodar::log::engine().info(__VA_ARGS__)
#define ZODAR_LOG_WARN(...)  ::zodar::log::engine().warn(__VA_ARGS__)
#define ZODAR_LOG_ERROR(...) ::zodar::log::engine().error(__VA_ARGS__)
