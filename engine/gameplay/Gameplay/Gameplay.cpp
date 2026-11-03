#include "Gameplay/Gameplay.h"
#include "Core/Error.h"
#include <cmath>

namespace zodar {
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
