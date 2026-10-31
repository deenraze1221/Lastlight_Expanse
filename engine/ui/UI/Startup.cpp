#include "UI/Startup.h"

#include <cmath>

namespace zodar::ui {

std::string_view stageLabel(StartupStage stage) {
    switch (stage) {
    case StartupStage::AnalyzingSystem: return "Analyzing system";
    case StartupStage::PreparingShaders: return "Preparing shaders";
    case StartupStage::ExtractingResources: return "Extracting resources";
    case StartupStage::ConfiguringSettings: return "Configuring settings";
    case StartupStage::PreparingEngine: return "Preparing engine";
    case StartupStage::InitializingComponents: return "Initializing components";
    case StartupStage::Count: break;
    }
    return {};
}

const std::array<StartupStage, kStartupStageCount>& startupStages() {
    static const std::array<StartupStage, kStartupStageCount> stages{
        StartupStage::AnalyzingSystem,     StartupStage::PreparingShaders, StartupStage::ExtractingResources,
        StartupStage::ConfiguringSettings, StartupStage::PreparingEngine,  StartupStage::InitializingComponents};
    return stages;
}

bool showFirstLaunchHint(const StartupSnapshot& s) {
    return s.state == StartupState::Running && s.stage == StartupStage::PreparingShaders &&
           s.stageElapsedSeconds > kFirstLaunchHintSeconds;
}

std::string formatCounts(const std::optional<StageCounts>& counts) {
    if (!counts || counts->total == 0) {
        return {};
    }
    std::string out = counts->unit;
    if (!out.empty()) out += ' ';
    out += std::to_string(counts->done) + "/" + std::to_string(counts->total);
    return out;
}

bool hasExactProgress(const StartupSnapshot& s) {
    return s.overall.has_value() && std::isfinite(*s.overall);
}

} // namespace zodar::ui
