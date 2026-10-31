#pragma once

// Standard command line for every zodar::Application, so agents run every app the same way:
//
//   --frames N             Render N frames, then exit (0 = until the window closes)
//   --headless             No visible window (SDL offscreen driver); needs --frames
//   --deterministic        Every frame advances exactly one fixed step (see --fixed-dt)
//   --fixed-dt S           Fixed simulation step in seconds (default 1/60)
//   --width W --height H   Window / render size
//   --screenshot F         Save the last frame (.png or .ppm); needs --frames
//   --capture-frames LIST  Also save these 1-based frames, e.g. 10,60,120 ...
//   --capture-dir DIR      ... as DIR/frame_000010.png (default: captures)
//   --report F.json        Write a run report at exit
//   --log-file F           Also write the log to F
//   --seed N               App-defined seed
//   --camera X,Y,Z,YAW,PITCH  Start camera (degrees), for apps that have a camera
//   --threads N            Worker threads (0 = run jobs inline)
//   --no-validation        Disable Vulkan validation layers
//   --overlay / --no-overlay  Profiler overlay visible at start
//   --help                 Print usage and exit
//
// Exit codes: 0 ok, 1 fatal error or bad arguments, 2 Vulkan validation errors,
// 3 screenshot/capture missing.

#include "Runtime/Application.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zodar {

// Parses the standard options into `desc` (other fields keep their values). Returns the
// arguments it does not know, in order, for the app's own parser. `--help` is returned too.
// Throws zodar::Error on a missing or invalid value.
std::vector<std::string> parseStandardOptions(std::span<const std::string> args, ApplicationDesc& desc);

// Strict number parsing for app options (whole text must be a number; floats must be finite).
// Throws zodar::Error naming `option` otherwise.
int64_t parseIntArgument(std::string_view option, std::string_view text);
double parseFloatArgument(std::string_view option, std::string_view text);

// Usage text of the standard options (one option per line).
std::string_view standardOptionsHelp();

// Builds the app from the parsed settings and the app-specific arguments.
using ApplicationFactory =
    std::function<std::unique_ptr<Application>(const ApplicationDesc& desc, std::span<const std::string> appArgs)>;

// Standard main(): parses the standard options over `defaults`, opens the log (--log-file),
// prints help for --help (`appUsage` first, then the standard options), constructs the app,
// runs it and returns the exit code. Exceptions (bad arguments, startup failures) return 1.
//
//   int main(int argc, char** argv) {
//       return zodar::runMain(argc, argv, desc, "MyApp [--my-option]", [](auto& d, auto args) {
//           return std::make_unique<MyApp>(d, parseMyOptions(args));
//       });
//   }
int runMain(int argc, char** argv, ApplicationDesc defaults, std::string_view appUsage,
            const ApplicationFactory& factory);

} // namespace zodar
