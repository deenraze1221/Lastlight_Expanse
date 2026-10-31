#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace zodar::ui {

// Engine startup as shown by the loading window. The UI only displays what a progress
// source reports; it never invents counts or percentages. When no exact progress is
// reported, the loading screen shows an indeterminate animation instead.
enum class StartupStage : uint8_t {
    AnalyzingSystem,
    PreparingShaders, // Approved label (was "Compiling shaders"); never shows invented counts.
    ExtractingResources,
    ConfiguringSettings,
    PreparingEngine,
    InitializingComponents,
    Count
};
inline constexpr size_t kStartupStageCount = static_cast<size_t>(StartupStage::Count);

// Labels in the approved order, exactly as specified.
std::string_view stageLabel(StartupStage stage);
const std::array<StartupStage, kStartupStageCount>& startupStages();

enum class StartupState : uint8_t { Running, Failed, Completed };

// Exact item counts for the current stage, e.g. {"Files", 812, 1240}. Only filled when the
// engine system doing the work reports real numbers.
struct StageCounts {
    std::string unit;
    uint64_t done = 0;
    uint64_t total = 0;
};

struct StartupSnapshot {
    StartupStage stage = StartupStage::AnalyzingSystem;
    StartupState state = StartupState::Running;
    double stageElapsedSeconds = 0.0;   // Time spent in the current stage (real clock).
    std::optional<float> overall;        // 0..1, only if the source knows the exact overall progress.
    std::optional<StageCounts> counts;   // Only real counts.
    std::string error;                   // Failure message (state == Failed).
};

// Source of startup progress. The engine implements it once its systems exist; until then
// only test/sandbox mocks implement it (they must label themselves as mock data).
class IStartupProgress {
public:
    virtual ~IStartupProgress() = default;
    virtual StartupSnapshot snapshot() const = 0;
    // Restart from the failed stage. Earlier completed stages stay completed.
    virtual void retry() = 0;
};

// Approved first-launch hint: shown only while Preparing shaders has run longer than 30 s.
inline constexpr double kFirstLaunchHintSeconds = 30.0;
inline constexpr std::string_view kFirstLaunchHint = "First launch \xE2\x80\x94 shaders are being cached";
bool showFirstLaunchHint(const StartupSnapshot& snapshot);

// "Files 812/1240"; empty when no real counts are reported or total is zero.
std::string formatCounts(const std::optional<StageCounts>& counts);

// True when the snapshot carries exact progress (bar can be determinate).
bool hasExactProgress(const StartupSnapshot& snapshot);

} // namespace zodar::ui
