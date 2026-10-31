#include "Core/Log.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/os.h>
#include <spdlog/fmt/fmt.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <iterator>
#include <string_view>
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

void append(spdlog::memory_buf_t& out, std::string_view text) {
    out.append(text.data(), text.data() + text.size());
}

// Length of the valid UTF-8 sequence at text[i], or 0 if it is invalid (stray continuation
// byte, overlong lead, truncated sequence, > U+10FFFF).
size_t utf8SequenceLength(spdlog::string_view_t text, size_t i) {
    const auto byte = [&](size_t k) { return static_cast<unsigned char>(text[k]); };
    const unsigned char lead = byte(i);
    const size_t length = lead >= 0xC2 && lead <= 0xDF ? 2 : lead >= 0xE0 && lead <= 0xEF ? 3
                        : lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
    if (length == 0 || i + length > text.size()) return 0;
    for (size_t k = 1; k < length; ++k) {
        if ((byte(i + k) & 0xC0) != 0x80) return 0;
    }
    if ((lead == 0xE0 && byte(i + 1) < 0xA0) || (lead == 0xF0 && byte(i + 1) < 0x90) ||
        (lead == 0xF4 && byte(i + 1) > 0x8F) || (lead == 0xED && byte(i + 1) > 0x9F)) {
        return 0; // overlong, beyond U+10FFFF, or a UTF-16 surrogate
    }
    return length;
}

// JSON string; invalid UTF-8 (e.g. ANSI code page paths on Windows) becomes U+FFFD.
void appendJsonString(spdlog::memory_buf_t& out, spdlog::string_view_t text) {
    out.push_back('"');
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        switch (c) {
        case '"': append(out, "\\\""); break;
        case '\\': append(out, "\\\\"); break;
        case '\n': append(out, "\\n"); break;
        case '\r': append(out, "\\r"); break;
        case '\t': append(out, "\\t"); break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                fmt::format_to(std::back_inserter(out), "\\u{:04x}", static_cast<unsigned>(c));
            } else if (static_cast<unsigned char>(c) < 0x80) {
                out.push_back(c);
            } else if (const size_t length = utf8SequenceLength(text, i); length > 0) {
                out.append(text.data() + i, text.data() + i + length);
                i += length - 1;
            } else {
                append(out, "\\ufffd");
            }
        }
    }
    out.push_back('"');
}

// JSON Lines sink: {"time":"2026-01-02T03:04:05.678Z","level":"info","thread":123,"msg":"..."}
class JsonLinesSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit JsonLinesSink(const std::string& path) { m_file.open(path, true); }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        const auto seconds = std::chrono::system_clock::to_time_t(msg.time);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(msg.time.time_since_epoch()).count() % 1000;
        const std::tm tm = spdlog::details::os::gmtime(seconds);
        spdlog::memory_buf_t line;
        fmt::format_to(std::back_inserter(line), "{{\"time\":\"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z\",\"level\":",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
        const spdlog::string_view_t level = spdlog::level::to_string_view(msg.level);
        appendJsonString(line, level);
        fmt::format_to(std::back_inserter(line), ",\"thread\":{},\"msg\":", msg.thread_id);
        appendJsonString(line, msg.payload);
        append(line, "}\n");
        m_file.write(line);
    }
    void flush_() override { m_file.flush(); }

private:
    spdlog::details::file_helper m_file;
};

std::shared_ptr<spdlog::logger> makeLogger(const Config& config) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    if (!config.filePath.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.filePath, true));
    }
    if (!config.jsonFilePath.empty()) {
        sinks.push_back(std::make_shared<JsonLinesSink>(config.jsonFilePath));
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
