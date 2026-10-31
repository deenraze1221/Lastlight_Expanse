#pragma once

#include "UI/Context.h"

#include <optional>

namespace zodar::ui {

// Moves the displayed fraction smoothly toward the reported one. Never shows more than was
// reported; jumps down immediately when the reported value drops (e.g. after Retry).
float smoothProgress(float displayed, float target, float deltaSeconds);

// Indeterminate animation: a segment (28% of the track) sweeping left to right every 1.4 s.
// Returned in the track's coordinate space; may extend past the track (callers clip).
inline constexpr float kIndeterminateWidth = 0.28f;
inline constexpr double kIndeterminatePeriod = 1.4;
Rect indeterminateSegment(Rect track, double timeSeconds);

// Thin progress bar. `fraction` set: determinate fill; nullopt: indeterminate animation.
void progressBar(Context& ctx, Rect logical, std::optional<float> fraction, double timeSeconds, Color fill,
                 Color track = color::kBrandSurface);

} // namespace zodar::ui
