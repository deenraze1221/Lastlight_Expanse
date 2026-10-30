#include "Core/Log.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <mutex>
#include <vector>

namespace raven::log {
namespace {

std::mutex g_mutex;
std::shared_ptr<spdlog::logger> g_logger;

std::shared_ptr<spdlog::logger> makeLogger(const Config& config) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    if (!config.filePath.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.filePath, true));
    }
    auto logger = std::make_shared<spdlog::logger>("RAVEN", sinks.begin(), sinks.end());
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    logger->set_level(config.level);
    logger->flush_on(spdlog::level::warn);
    return logger;
}

} // namespace

void init(const Config& config) {
    std::scoped_lock lock(g_mutex);
    g_logger = makeLogger(config);
}

void shutdown() {
    std::scoped_lock lock(g_mutex);
    if (g_logger) {
        g_logger->flush();
        g_logger.reset();
    }
}

bool isInitialized() {
    std::scoped_lock lock(g_mutex);
    return g_logger != nullptr;
}

spdlog::logger& engine() {
    std::scoped_lock lock(g_mutex);
    if (!g_logger) {
        g_logger = makeLogger({});
    }
    return *g_logger;
}

} // namespace raven::log
