#include "MockStartup.h"

#include "Core/Error.h"

#include <algorithm>
#include <cmath>

namespace zodar::sandbox {

MockScenario parseScenario(const std::string& n) {
    if (n == "normal") return MockScenario::Normal;
    if (n == "slow-shaders") return MockScenario::SlowShaders;
    if (n == "failure") return MockScenario::Failure;
    if (n == "indeterminate") return MockScenario::Indeterminate;
    fail("unknown --scenario (normal | slow-shaders | failure | indeterminate): " + n);
}

const char* scenarioName(MockScenario s) {
    switch (s) {
    case MockScenario::Normal: return "normal";
    case MockScenario::SlowShaders: return "slow-shaders";
    case MockScenario::Failure: return "failure";
    case MockScenario::Indeterminate: return "indeterminate";
    }
    return "?";
}

MockStartupProgress::MockStartupProgress(MockScenario scenario, ui::LogBuffer* log) : m_scenario(scenario), m_log(log) {
    // Example values only. Preparing shaders never reports counts (approved: no invented
    // shader numbers); it only advances the overall bar when the scenario reports one.
    m_script = {{{1.4, 0.06f, nullptr, 0},
                 {scenario == MockScenario::SlowShaders ? 40.0 : 5.0, 0.40f, nullptr, 0},
                 {3.2, 0.22f, "Files", 1240},
                 {1.0, 0.06f, nullptr, 0},
                 {1.5, 0.10f, nullptr, 0},
                 {2.2, 0.16f, "Modules", 24}}};
    logLine(ui::LogLevel::Info, std::string("[mock] scenario '") + scenarioName(scenario) +
                                    "': scripted example data, not engine progress");
    enter(0);
}

void MockStartupProgress::logLine(ui::LogLevel level, const std::string& text) {
    if (m_log) m_log->add({m_time, level, text});
}

void MockStartupProgress::enter(size_t stage) {
    m_stage = stage;
    m_stageStart = m_time;
    logLine(ui::LogLevel::Info, std::string("[mock] ") + std::string(ui::stageLabel(ui::startupStages()[stage])) + "\xE2\x80\xA6");
}

void MockStartupProgress::advance(double seconds) {
    double remaining = std::max(0.0, seconds);
    while (remaining > 0.0 && m_state == ui::StartupState::Running) {
        const StageScript& sc = m_script[m_stage];
        const double left = sc.seconds - (m_time - m_stageStart);
        // Failure scenario: the first attempt at Preparing shaders fails after 2 s.
        if (m_scenario == MockScenario::Failure && !m_failedOnce && m_stage == 1) {
            const double toFail = 2.0 - (m_time - m_stageStart);
            if (toFail <= remaining) {
                m_time += std::max(0.0, toFail);
                m_state = ui::StartupState::Failed;
                m_failedOnce = true;
                m_error = "[mock] shader 'pbr.frag' failed to build (example failure)";
                logLine(ui::LogLevel::Error, m_error);
                return;
            }
        }
        if (left > remaining) {
            m_time += remaining;
            return;
        }
        m_time += std::max(0.0, left);
        remaining -= std::max(0.0, left);
        logLine(ui::LogLevel::Info, std::string("[mock] ") + std::string(ui::stageLabel(ui::startupStages()[m_stage])) + " done");
        if (m_stage + 1 == ui::kStartupStageCount) {
            m_state = ui::StartupState::Completed;
            logLine(ui::LogLevel::Info, "[mock] startup complete");
            return;
        }
        enter(m_stage + 1);
    }
    if (m_state != ui::StartupState::Running) m_time += remaining;
}

ui::StartupSnapshot MockStartupProgress::snapshot() const {
    ui::StartupSnapshot s;
    s.stage = ui::startupStages()[m_stage];
    s.state = m_state;
    s.stageElapsedSeconds = m_time - m_stageStart;
    s.error = m_error;
    if (m_scenario == MockScenario::Indeterminate) {
        return s; // Reports no exact progress at all.
    }
    const StageScript& sc = m_script[m_stage];
    const double f = m_state == ui::StartupState::Completed ? 1.0 : std::clamp(s.stageElapsedSeconds / sc.seconds, 0.0, 1.0);
    float overall = 0.0f;
    for (size_t i = 0; i < m_stage; ++i) overall += m_script[i].weight;
    overall += sc.weight * static_cast<float>(f);
    s.overall = std::min(1.0f, overall);
    if (sc.unit) {
        s.counts = ui::StageCounts{sc.unit, static_cast<uint64_t>(std::floor(f * static_cast<double>(sc.items))), sc.items};
    }
    return s;
}

void MockStartupProgress::retry() {
    if (m_state != ui::StartupState::Failed) return;
    m_state = ui::StartupState::Running;
    m_error.clear();
    logLine(ui::LogLevel::Warning, std::string("[mock] retrying ") + std::string(ui::stageLabel(ui::startupStages()[m_stage])));
    m_stageStart = m_time; // Restart the failed stage; earlier stages stay done.
}

} // namespace zodar::sandbox
