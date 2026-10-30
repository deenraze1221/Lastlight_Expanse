#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace raven {

// Scoped CPU timers collected per frame. Single-threaded by design (frame thread); a
// multi-threaded variant comes with multi-threaded recording.
class CpuProfiler {
public:
    struct Scope {
        std::string name;
        int depth = 0;
        double ms = 0.0;
    };

    void beginFrame();
    void endFrame();
    uint32_t begin(const char* name);
    void end(uint32_t scope);

    // Results of the last completed frame.
    const std::vector<Scope>& lastFrame() const { return m_last; }
    double lastFrameMs() const { return m_lastFrameMs; }

    class Timer {
    public:
        Timer(CpuProfiler& profiler, const char* name) : m_profiler(profiler), m_scope(profiler.begin(name)) {}
        ~Timer() { m_profiler.end(m_scope); }
        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;

    private:
        CpuProfiler& m_profiler;
        uint32_t m_scope;
    };

private:
    using Clock = std::chrono::steady_clock;
    struct Open {
        Clock::time_point start;
    };
    std::vector<Scope> m_current;
    std::vector<Open> m_open;
    std::vector<Scope> m_last;
    int m_depth = 0;
    Clock::time_point m_frameStart;
    double m_lastFrameMs = 0.0;
    bool m_inFrame = false;
};

// Fixed-size history of a value (frame times) with simple statistics.
class History {
public:
    explicit History(size_t capacity = 240) : m_values(capacity, 0.0f) {}
    void push(float value);
    const std::vector<float>& values() const { return m_values; } // Ring buffer.
    size_t offset() const { return m_next; }
    size_t count() const { return m_count; }
    float average() const;
    float max() const;
    // Value below which `fraction` of samples fall (e.g. 0.99 for the 99th percentile).
    float percentile(float fraction) const;

private:
    std::vector<float> m_values;
    size_t m_next = 0;
    size_t m_count = 0;
};

} // namespace raven

#define RAVEN_CPU_CONCAT_INNER(a, b) a##b
#define RAVEN_CPU_CONCAT(a, b) RAVEN_CPU_CONCAT_INNER(a, b)
#define RAVEN_CPU_SCOPE(profiler, name) ::raven::CpuProfiler::Timer RAVEN_CPU_CONCAT(ravenCpuScope, __LINE__)(profiler, name)
