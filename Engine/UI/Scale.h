#pragma once

#include "UI/Geometry.h"

namespace zodar::ui {

// How a surface derives its UI scale.
enum class ScalePolicy {
    // Launcher, Settings, tools: physical density (display scale x user preference). The
    // layout then responds to the logical size (pixels / scale) that results.
    Density,
    // Fixed compositions such as the Loading window: fit a design canvas into the window.
    FitDesign,
};

struct ScaleInputs {
    Size2 windowPixels;            // Drawable size in physical pixels.
    float displayScale = 1.0f;     // OS content scale (e.g. 1.25 at 120 DPI, 2.0 on Retina).
    float userScale = 1.0f;        // User preference multiplier.
    Size2 designSize{1920, 1080};  // FitDesign: the canvas the composition is authored at.
};

inline constexpr float kMinScale = 0.5f;
inline constexpr float kMaxScale = 4.0f;

// Continuous, any value in [kMinScale, kMaxScale]; not limited to the test profiles.
float computeScale(ScalePolicy policy, const ScaleInputs& in);

// Scale rounded to 1/20 so glyph atlases are shared between nearly equal scales.
float quantizeScale(float scale);

// Logical (design-unit) size of the window at `scale`.
Size2 logicalSize(Size2 windowPixels, float scale);

// Test profiles used for screenshots and review (not the only supported scales).
inline constexpr float kTestProfiles[] = {1.0f, 1.33f, 2.0f};

} // namespace zodar::ui
