#include "Runtime/CommandLine.h"

#include "Core/Error.h"
#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <exception>
#include <system_error>
#include <type_traits>

namespace zodar {

namespace {

constexpr std::string_view kHelp =
    "Standard options:\n"
    "  --frames N                 Render N frames, then exit (0 = until the window closes)\n"
    "  --headless                 No visible window (SDL offscreen driver); needs --frames\n"
    "  --deterministic            Every frame advances exactly one fixed step\n"
    "  --fixed-dt S               Fixed simulation step in seconds (default 1/60)\n"
    "  --width W --height H       Window / render size\n"
    "  --screenshot F             Save the last frame (.png or .ppm); needs --frames\n"
    "  --capture-frames LIST      Also save these 1-based frames, e.g. 10,60,120\n"
    "  --capture-dir DIR          Directory for --capture-frames (default: captures)\n"
    "  --report F.json            Write a run report at exit\n"
    "  --log-file F               Also write the log to F\n"
    "  --log-json F.jsonl         Also write the log as JSON lines to F\n"
    "  --seed N                   App-defined seed\n"
    "  --camera X,Y,Z,YAW,PITCH   Start camera (degrees), for apps that have a camera\n"
    "  --threads N                Worker threads (0 = run jobs inline)\n"
    "  --no-validation            Disable Vulkan validation layers\n"
    "  --sync-validation          Also enable synchronization validation (slower)\n"
    "  --overlay | --no-overlay   Profiler overlay visible at start\n"
    "  --help                     Print usage and exit\n"
    "Exit codes: 0 ok, 1 fatal error or bad arguments, 2 Vulkan validation errors,\n"
    "3 screenshot/capture missing.\n";

template <typename T>
T parseNumber(std::string_view option, std::string_view text) {
    T value{};
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (text.empty() || ec != std::errc() || ptr != end) {
        fail("Invalid value '" + std::string(text) + "' for " + std::string(option));
    }
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) fail("Invalid value '" + std::string(text) + "' for " + std::string(option));
    }
    return value;
}

std::filesystem::path imagePath(std::string_view option, std::string_view text) {
    std::filesystem::path path{std::string(text)};
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".png" && ext != ".ppm") fail(std::string(option) + " needs a .png or .ppm file name");
    return path;
}

std::vector<std::string_view> splitList(std::string_view text) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t comma = text.find(',', start);
        parts.push_back(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return parts;
}

} // namespace

std::vector<std::string> parseStandardOptions(std::span<const std::string> args, ApplicationDesc& desc) {
    std::vector<std::string> rest;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= args.size()) fail("Missing value for " + std::string(arg));
            return args[++i];
        };
        if (arg == "--frames") desc.maxFrames = parseNumber<uint64_t>(arg, next());
        else if (arg == "--headless") desc.headless = true;
        else if (arg == "--deterministic") desc.deterministic = true;
        else if (arg == "--fixed-dt") {
            const double dt = parseNumber<double>(arg, next());
            if (!(dt > 0.0) || dt > 1.0) fail("--fixed-dt must be in (0, 1] seconds");
            desc.timestep.fixedDt = dt;
            desc.timestep.maxFrameTime = std::max(desc.timestep.maxFrameTime, dt);
        } else if (arg == "--width") desc.width = parseNumber<uint32_t>(arg, next());
        else if (arg == "--height") desc.height = parseNumber<uint32_t>(arg, next());
        else if (arg == "--screenshot") desc.screenshot = imagePath(arg, next());
        else if (arg == "--capture-frames") {
            desc.captureFrames.clear();
            for (std::string_view part : splitList(next())) {
                const auto frame = parseNumber<uint64_t>(arg, part);
                if (frame == 0) fail("--capture-frames uses 1-based frame numbers");
                desc.captureFrames.push_back(frame);
            }
            std::sort(desc.captureFrames.begin(), desc.captureFrames.end());
            desc.captureFrames.erase(std::unique(desc.captureFrames.begin(), desc.captureFrames.end()),
                                     desc.captureFrames.end());
        } else if (arg == "--capture-dir") desc.captureDir = std::filesystem::path(std::string(next()));
        else if (arg == "--report") desc.reportPath = std::filesystem::path(std::string(next()));
        else if (arg == "--log-file") desc.logFile = std::filesystem::path(std::string(next()));
        else if (arg == "--log-json") desc.jsonLogFile = std::filesystem::path(std::string(next()));
        else if (arg == "--seed") desc.seed = parseNumber<uint64_t>(arg, next());
        else if (arg == "--camera") {
            const auto parts = splitList(next());
            if (parts.size() != 5) fail("--camera expects X,Y,Z,YAW,PITCH");
            CameraOverride camera;
            camera.position = {parseNumber<float>(arg, parts[0]), parseNumber<float>(arg, parts[1]),
                               parseNumber<float>(arg, parts[2])};
            camera.yawDegrees = parseNumber<float>(arg, parts[3]);
            camera.pitchDegrees = parseNumber<float>(arg, parts[4]);
            desc.camera = camera;
        } else if (arg == "--threads") desc.threads = parseNumber<uint32_t>(arg, next());
        else if (arg == "--no-validation") desc.validation = false;
        else if (arg == "--sync-validation") desc.synchronizationValidation = true;
        else if (arg == "--overlay") desc.overlay = true;
        else if (arg == "--no-overlay") desc.overlay = false;
        else rest.emplace_back(arg);
    }
    if (desc.width == 0 || desc.height == 0) fail("--width and --height must be > 0");
    return rest;
}

int64_t parseIntArgument(std::string_view option, std::string_view text) {
    return parseNumber<int64_t>(option, text);
}

double parseFloatArgument(std::string_view option, std::string_view text) {
    return parseNumber<double>(option, text);
}

std::string_view standardOptionsHelp() {
    return kHelp;
}

int runMain(int argc, char** argv, ApplicationDesc defaults, std::string_view appUsage,
            const ApplicationFactory& factory) {
    log::init({spdlog::level::info, ""});
    int exitCode = 1;
    try {
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        ApplicationDesc desc = std::move(defaults);
        std::vector<std::string> appArgs = parseStandardOptions(args, desc);
        if (std::find(appArgs.begin(), appArgs.end(), "--help") != appArgs.end()) {
            std::fputs(std::string(appUsage).append("\n").c_str(), stdout);
            std::fputs(kHelp.data(), stdout);
            log::shutdown();
            return 0;
        }
        if (!desc.logFile.empty() || !desc.jsonLogFile.empty()) {
            log::init({spdlog::level::info, desc.logFile.string(), desc.jsonLogFile.string()});
        }
        std::unique_ptr<Application> app = factory(desc, appArgs);
        exitCode = app->run();
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
        exitCode = 1;
    }
    ZODAR_LOG_INFO("Shutdown complete (exit code {})", exitCode);
    log::shutdown();
    return exitCode;
}

} // namespace zodar
