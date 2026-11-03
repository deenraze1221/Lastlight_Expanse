#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace zodar {
class CpuProfiler;
enum class GameplayPhase { Input, FixedSimulation, Animation, LateUpdate, RenderSync };
struct SystemContext {
    double dt = 0;
    uint64_t tick = 0; // fixed: executing tick (1-based); presentation: last completed tick
    double alpha = 0; // presentation interpolation; zero during fixed simulation
};
struct SystemDesc {
    std::string name;
    GameplayPhase phase = GameplayPhase::FixedSimulation;
    std::function<void(const SystemContext&)> run;
    std::vector<std::string> before, after;
};
// Main-thread, sequential execution. Registration freezes on validation/first run.
class SystemScheduler {
public:
    void add(SystemDesc system);
    void validate();
    void run(GameplayPhase phase, const SystemContext& context, CpuProfiler* profiler = nullptr);
private:
    struct Entry { SystemDesc system; std::string profileName; };
    std::vector<Entry> m_systems;
    std::vector<std::vector<size_t>> m_order;
    bool m_validated = false;
    bool m_running = false;
};
} // namespace zodar
