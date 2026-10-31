#include <doctest/doctest.h>

#include "Runtime/CommandLine.h"

#include <string>
#include <vector>

using zodar::ApplicationDesc;
using zodar::parseStandardOptions;

namespace {

std::vector<std::string> parse(std::vector<std::string> args, ApplicationDesc& desc) {
    return parseStandardOptions(args, desc);
}

} // namespace

TEST_CASE("CommandLine parses the standard options") {
    ApplicationDesc desc;
    const auto rest = parse({"--frames", "120", "--headless", "--deterministic", "--fixed-dt", "0.5", "--width", "640",
                             "--height", "360", "--screenshot", "out/shot.png", "--capture-frames", "60,10,60,120",
                             "--capture-dir", "caps", "--report", "run.json", "--log-file", "run.log", "--seed", "42",
                             "--camera", "1.5,-2,3,90,-10", "--threads", "0", "--no-validation", "--overlay"},
                            desc);
    CHECK(rest.empty());
    CHECK(desc.maxFrames == 120);
    CHECK(desc.headless);
    CHECK(desc.deterministic);
    CHECK(desc.timestep.fixedDt == 0.5);
    CHECK(desc.timestep.maxFrameTime >= 0.5); // kept valid for FixedTimestep
    CHECK(desc.width == 640);
    CHECK(desc.height == 360);
    REQUIRE(desc.screenshot);
    CHECK(*desc.screenshot == "out/shot.png");
    CHECK(desc.captureFrames == std::vector<uint64_t>{10, 60, 120}); // sorted, unique
    CHECK(desc.captureDir == "caps");
    REQUIRE(desc.reportPath);
    CHECK(*desc.reportPath == "run.json");
    CHECK(desc.logFile == "run.log");
    CHECK(desc.seed == 42u);
    REQUIRE(desc.camera);
    CHECK(desc.camera->position.x == 1.5f);
    CHECK(desc.camera->position.y == -2.0f);
    CHECK(desc.camera->yawDegrees == 90.0f);
    CHECK(desc.camera->pitchDegrees == -10.0f);
    CHECK(desc.threads == 0);
    CHECK_FALSE(desc.validation);
    CHECK(desc.overlay);
}

TEST_CASE("CommandLine keeps defaults and returns app arguments in order") {
    ApplicationDesc desc;
    desc.width = 1600;
    desc.seed = 1337;
    const auto rest = parse({"--view-radius", "5", "--frames", "3", "--untextured", "--help"}, desc);
    CHECK(rest == std::vector<std::string>{"--view-radius", "5", "--untextured", "--help"});
    CHECK(desc.maxFrames == 3);
    CHECK(desc.width == 1600);
    CHECK(desc.seed == 1337u);
    CHECK_FALSE(desc.headless);
    CHECK_FALSE(desc.reportPath);
}

TEST_CASE("CommandLine rejects missing and invalid values") {
    ApplicationDesc desc;
    CHECK_THROWS(parse({"--frames"}, desc));
    CHECK_THROWS(parse({"--frames", "ten"}, desc));
    CHECK_THROWS(parse({"--frames", "-1"}, desc));
    CHECK_THROWS(parse({"--width", "0"}, desc));
    CHECK_THROWS(parse({"--fixed-dt", "0"}, desc));
    CHECK_THROWS(parse({"--capture-frames", "0,5"}, desc));
    CHECK_THROWS(parse({"--capture-frames", "5,,6"}, desc));
    CHECK_THROWS(parse({"--camera", "1,2,3"}, desc));
    CHECK_THROWS(parse({"--seed", "12x"}, desc));
    CHECK_THROWS(parse({"--camera", "nan,0,0,0,0"}, desc));
    CHECK_THROWS(parse({"--camera", "inf,0,0,0,0"}, desc));
    CHECK_THROWS(parse({"--fixed-dt", "nan"}, desc));
    CHECK_THROWS(parse({"--screenshot", "shot.jpg"}, desc));
    CHECK_THROWS(parse({"--screenshot", "shot"}, desc));
    CHECK(zodar::parseIntArgument("--x", "-5") == -5);
    CHECK_THROWS(zodar::parseIntArgument("--x", "10abc"));
    CHECK(zodar::parseFloatArgument("--x", "-12.5") == -12.5);
    CHECK_THROWS(zodar::parseFloatArgument("--x", "1e999"));
}

TEST_CASE("CommandLine help lists every standard option") {
    const std::string help(zodar::standardOptionsHelp());
    for (const char* option : {"--frames", "--headless", "--deterministic", "--fixed-dt", "--width", "--screenshot",
                               "--capture-frames", "--capture-dir", "--report", "--log-file", "--seed", "--camera",
                               "--threads", "--no-validation", "--overlay", "--help"}) {
        CHECK_MESSAGE(help.find(option) != std::string::npos, option);
    }
}
