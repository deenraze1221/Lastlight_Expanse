#pragma once

#include "UI/Geometry.h"

#include <array>

namespace zodar::ui {

// Window placement for the single ZODAR window: loading first, then the launcher. Pure
// functions in physical pixels; the platform layer (today: the sandbox) applies them.
struct DisplayArea {
    Rect bounds; // Whole monitor.
    Rect usable; // Monitor minus taskbar/dock.
};

// Approved loading window sizes (16:9). Approved mapping:
//   1920x1080 -> 960x540, 2560x1440 -> 1280x720, 3840x2160 -> 1920x1080.
inline constexpr std::array<Size2, 3> kLoadingProfiles{{{960, 540}, {1280, 720}, {1920, 1080}}};
inline constexpr Size2 kLoadingDesignSize{960, 540};

// Largest profile no bigger than half the monitor in both dimensions (reproduces the approved
// table exactly). Monitors smaller than 1920x1080 get 960x540. Any result that does not fit
// 90% of the usable area is shrunk at 16:9 to fit. Centred in the usable area. Never fullscreen.
Rect loadingWindowRect(const DisplayArea& display);

// Launcher after loading (rule not yet confirmed by design): the largest 16:9 window that fits
// 85% of the usable area, capped at 1920x1080 design units x display scale, centred.
Rect launcherWindowRect(const DisplayArea& display, float displayScale);

// `size` centred in `area`, on whole pixels.
Rect centeredIn(Rect area, Size2 size);

} // namespace zodar::ui
