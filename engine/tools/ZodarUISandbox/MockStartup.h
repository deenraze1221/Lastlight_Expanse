#pragma once

#include "UI/LogBuffer.h"
#include "UI/Startup.h"

#include <array>
#include <string>

namespace zodar::sandbox {

// MOCK startup progress for the temporary sandbox only. It is NOT engine progress: stage
// durations, counts and failures are scripted example values for reviewing the loading
// window. Every log line it writes is prefixed with "[mock]".
enum class MockScenario { Normal, SlowShaders, Failure, Indeterminate };
MockScenario parseScenario(const std::string& name); // Fails on unknown names.
const char* scenarioName(MockScenario scenario);

class MockStartupProgress final : public ui::IStartupProgress {
public:
    MockStartupProgress(MockScenario scenario, ui::LogBuffer* log);

    // Advances mock time (seconds). Stage transitions and failures happen here.
    void advance(double seconds);
    double time() const { return m_time; }

    ui::StartupSnapshot snapshot() const override;
    void retry() override;

private:
    struct StageScript {
        double seconds;
        float weight;              // Share of the overall bar.
        const char* unit;          // Exact-count unit, or nullptr when the stage has no counts.
        uint64_t items;
    };

    void enter(size_t stage);
    void logLine(ui::LogLevel level, const std::string& text);

    MockScenario m_scenario;
    ui::LogBuffer* m_log;
    std::array<StageScript, ui::kStartupStageCount> m_script{};
    double m_time = 0.0;
    double m_stageStart = 0.0;
    size_t m_stage = 0;
    ui::StartupState m_state = ui::StartupState::Running;
    bool m_failedOnce = false;
    std::string m_error;
};

} // namespace zodar::sandbox
