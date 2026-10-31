#include "Core/Profiler.h"

#include "Core/Error.h"

#include <algorithm>

namespace zodar {

void CpuProfiler::beginFrame() {
    m_current.clear();
    m_open.clear();
    m_depth = 0;
    m_frameStart = Clock::now();
    m_inFrame = true;
}

void CpuProfiler::endFrame() {
    ZODAR_ENSURE(m_depth == 0, "CpuProfiler::endFrame with open scopes");
    m_lastFrameMs = std::chrono::duration<double, std::milli>(Clock::now() - m_frameStart).count();
    m_last = std::move(m_current);
    m_current.clear();
    m_inFrame = false;
}

uint32_t CpuProfiler::begin(const char* name) {
    m_current.push_back({name, m_depth++, 0.0});
    m_open.push_back({Clock::now()});
    return static_cast<uint32_t>(m_current.size() - 1);
}

void CpuProfiler::end(uint32_t scope) {
    ZODAR_ENSURE(scope < m_current.size(), "invalid CPU profiler scope");
    m_current[scope].ms = std::chrono::duration<double, std::milli>(Clock::now() - m_open[scope].start).count();
    --m_depth;
}

void History::push(float value) {
    m_values[m_next] = value;
    m_next = (m_next + 1) % m_values.size();
    m_count = std::min(m_count + 1, m_values.size());
}

float History::average() const {
    if (m_count == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < m_count; ++i) sum += m_values[i];
    return static_cast<float>(sum / static_cast<double>(m_count));
}

float History::max() const {
    float m = 0.0f;
    for (size_t i = 0; i < m_count; ++i) m = std::max(m, m_values[i]);
    return m;
}

float History::percentile(float fraction) const {
    if (m_count == 0) return 0.0f;
    std::vector<float> sorted(m_values.begin(), m_values.begin() + static_cast<std::ptrdiff_t>(m_count));
    std::sort(sorted.begin(), sorted.end());
    const size_t index = std::min(m_count - 1, static_cast<size_t>(fraction * static_cast<float>(m_count - 1) + 0.5f));
    return sorted[index];
}

} // namespace zodar
