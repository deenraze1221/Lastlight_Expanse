#pragma once

#include "UI/Geometry.h"
#include "UI/Theme.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct SDL_Renderer;

namespace zodar::sandbox {

// Sandbox demo of the single-window flow: borderless loading window (approved size for the
// monitor, centred) driven by MOCK startup data, then resized and re-centred for the launcher.
struct LoadingDemoOptions {
    std::string scenario = "normal";
    std::optional<double> at;            // Freeze mock time at T seconds (screenshots).
    double speed = 1.0;                  // Mock time multiplier for live runs.
    std::optional<ui::Size2> monitor;    // Simulated monitor (pixels) instead of the real one.
    bool software = false;
    int frames = 0;                      // Stop after N frames (0 = run until closed).
    bool exitAfterLauncher = false;      // Live run: stop once the launcher window is shown.
    bool openLog = false;                // Failure scenario: show the log panel (screenshots).
    std::filesystem::path resources;
};

struct PixelProbe {
    std::string name;
    int x = 0;
    int y = 0;
    ui::Color expected;
};

// Called on the last frame (screenshot, pixel checks). Returns the process exit code.
using CaptureFn = std::function<int(SDL_Renderer*, const std::vector<PixelProbe>&)>;

int runLoadingDemo(const LoadingDemoOptions& options, const CaptureFn& capture);

} // namespace zodar::sandbox
