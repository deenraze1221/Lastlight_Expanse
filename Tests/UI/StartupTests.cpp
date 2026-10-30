#include <doctest/doctest.h>

#include "UI/Startup.h"

#include <limits>

using namespace zodar::ui;

TEST_CASE("ui startup: stages and labels in the approved order") {
    const char* expected[] = {"Analyzing system",     "Preparing shaders", "Extracting resources",
                              "Configuring settings", "Preparing engine",  "Initializing components"};
    REQUIRE(startupStages().size() == 6);
    for (size_t i = 0; i < 6; ++i) {
        CHECK(stageLabel(startupStages()[i]) == expected[i]);
    }
    for (StartupStage s : startupStages()) CHECK(stageLabel(s).find("Compiling") == std::string_view::npos);
}

TEST_CASE("ui startup: first-launch hint only after 30 s of Preparing shaders") {
    StartupSnapshot s;
    s.stage = StartupStage::PreparingShaders;
    s.stageElapsedSeconds = 29.9;
    CHECK_FALSE(showFirstLaunchHint(s));
    s.stageElapsedSeconds = 30.1;
    CHECK(showFirstLaunchHint(s));
    s.state = StartupState::Failed;
    CHECK_FALSE(showFirstLaunchHint(s));
    s.state = StartupState::Running;
    s.stage = StartupStage::ExtractingResources;
    s.stageElapsedSeconds = 300;
    CHECK_FALSE(showFirstLaunchHint(s));
    CHECK(kFirstLaunchHint == "First launch \xE2\x80\x94 shaders are being cached");
}

TEST_CASE("ui startup: counts are shown only when really reported") {
    CHECK(formatCounts(std::nullopt).empty());
    CHECK(formatCounts(StageCounts{"Files", 0, 0}).empty()); // Unknown total: nothing to show.
    CHECK(formatCounts(StageCounts{"Files", 812, 1240}) == "Files 812/1240");
    CHECK(formatCounts(StageCounts{"", 3, 7}) == "3/7");
    StartupSnapshot s;
    CHECK_FALSE(hasExactProgress(s));
    s.overall = 0.4f;
    CHECK(hasExactProgress(s));
    s.overall = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(hasExactProgress(s));
}
