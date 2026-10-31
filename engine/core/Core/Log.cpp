#include "Core/Log.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace zodar::log {
namespace {

std::mutex g_mutex;
std::shared_ptr<spdlog::logger> g_logger;
std::atomic<uint64_t> g_warnings{0};
std::atomic<uint64_t> g_errors{0};

// Counts warnings and errors; writes nothing.
class CountingSink final : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (msg.level == spdlog::level::warn) ++g_warnings;
        else if (msg.level >= spdlog::level::err && msg.level != spdlog::level::off) ++g_errors;
    }
    void flush_() override {}
};

std::shared_ptr<spdlog::logger> makeLogger(const Config& config) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    if (!config.filePath.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.filePath, true));
    }
    sinks.push_back(std::make_shared<CountingSink>());
    auto logger = std::make_shared<spdlog::logger>("ZODAR", sinks.begin(), sinks.end());
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

Counts counts() {
    return {g_warnings.load(), g_errors.load()};
}

void resetCounts() {
    g_warnings = 0;
    g_errors = 0;
}

} // namespace zodar::log
