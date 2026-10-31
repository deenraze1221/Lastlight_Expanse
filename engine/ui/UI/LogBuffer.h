#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace zodar::ui {

enum class LogLevel : uint8_t { Info, Warning, Error };

struct LogLine {
    double seconds = 0.0; // Since startup.
    LogLevel level = LogLevel::Info;
    std::string text;
};

// Bounded list of recent log lines for the loading window's log panel. Filled by whatever
// produces the log (later: an engine log sink; today: tests and the sandbox mock).
class LogBuffer {
public:
    explicit LogBuffer(size_t capacity = 500);

    void add(LogLine line);
    void clear() { m_lines.clear(); }
    const std::deque<LogLine>& lines() const { return m_lines; }
    size_t capacity() const { return m_capacity; }
    size_t dropped() const { return m_dropped; } // Lines discarded because the buffer was full.

private:
    size_t m_capacity;
    size_t m_dropped = 0;
    std::deque<LogLine> m_lines;
};

// "[012.34] text" with the seconds zero-padded to keep columns aligned.
std::string formatLogLine(const LogLine& line);

} // namespace zodar::ui
