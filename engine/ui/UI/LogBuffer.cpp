#include "UI/LogBuffer.h"

#include <algorithm>
#include <cstdio>

namespace zodar::ui {

LogBuffer::LogBuffer(size_t capacity) : m_capacity(std::max<size_t>(1, capacity)) {}

void LogBuffer::add(LogLine line) {
    if (m_lines.size() == m_capacity) {
        m_lines.pop_front();
        ++m_dropped;
    }
    m_lines.push_back(std::move(line));
}

std::string formatLogLine(const LogLine& line) {
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "[%06.2f] ", std::max(0.0, line.seconds));
    return stamp + line.text;
}

} // namespace zodar::ui
