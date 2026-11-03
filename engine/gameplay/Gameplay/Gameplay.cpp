#include "Gameplay/Gameplay.h"
#include "Core/Error.h"
#include <cmath>

namespace zodar {
Gameplay::Snapshot Gameplay::capture() const {
    if (m_active || m_timers.tick() != m_completed) fail("save requires a completed fixed tick");
    return {m_completed, m_timers.capture(), m_events.capture()};
}
Gameplay::Staged Gameplay::stage(const Snapshot& s) const {
    if (m_active || s.tick != s.timers.tick) fail("inconsistent saved gameplay clock");
    return {s.tick, m_timers.stage(s.timers), m_events.stage(s.events)};
}
void Gameplay::commit(Staged&& staged) noexcept {
    m_timers.commit(std::move(staged.timers)); m_events.commit(std::move(staged.events)); m_completed = staged.tick;
}
void Gameplay::step(double dt, const std::function<void()>& hook, CpuProfiler* profiler) {
    if (!std::isfinite(dt) || dt <= 0) fail("gameplay requires a positive finite fixed dt");
    if (m_active || m_timers.tick() != m_completed) fail("recursive or inconsistent gameplay tick");
    m_systems.validate();
    m_timers.beginTick();
    m_active = true;
    try {
        const SystemContext context{dt, m_timers.tick(), 0};
        for (const auto phase : {GameplayPhase::Input, GameplayPhase::FixedSimulation, GameplayPhase::Animation})
            m_systems.run(phase, context, profiler);
        if (hook) hook();
        m_timers.finishTick();
        m_events.advance();
        m_completed = context.tick;
    } catch (...) { m_active = false; throw; }
    m_active = false;
}
} // namespace zodar
