#pragma once
#include "Gameplay/SystemScheduler.h"
#include "Gameplay/EventBus.h"
#include "Gameplay/Timers.h"

namespace zodar {
// Runtime-owned sequential simulation. No player, scene or renderer ownership here.
class Gameplay {
public:
    SystemScheduler& systems() { return m_systems; }
    EventBus& events() { return m_events; }
    Timers& timers() { return m_timers; }
    struct Snapshot { uint64_t tick; Timers::Snapshot timers; EventBus::Snapshot events; };
    struct Staged { uint64_t tick; Timers timers; EventBus events; };
    Snapshot capture() const;
    Staged stage(const Snapshot& snapshot) const;
    void commit(Staged&& staged) noexcept;
    uint64_t completedTicks() const { return m_completed; }
    // Input has already been sampled. Hook follows fixed systems; timers then event flip
    // complete the tick. Only successful return establishes a checkpoint boundary.
    void step(double dt, const std::function<void()>& hook = {}, CpuProfiler* profiler = nullptr);
private:
    SystemScheduler m_systems;
    EventBus m_events;
    Timers m_timers;
    uint64_t m_completed = 0;
    bool m_active = false;
};
} // namespace zodar
