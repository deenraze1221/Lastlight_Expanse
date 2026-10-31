#include "UI/WindowPlan.h"

#include <algorithm>
#include <cmath>

namespace zodar::ui {
namespace {

// Largest 16:9 size (even width, whole pixels) fitting inside `box`, capped at `cap`.
Size2 fit169(Size2 box, Size2 cap) {
    float w = std::min(box.width, cap.width);
    float h = w * 9.0f / 16.0f;
    if (h > box.height) {
        h = box.height;
        w = h * 16.0f / 9.0f;
    }
    w = std::floor(w / 16.0f) * 16.0f; // Multiple of 16 keeps the height an exact integer.
    return {std::max(16.0f, w), std::max(9.0f, w * 9.0f / 16.0f)};
}

} // namespace

Rect centeredIn(Rect area, Size2 size) {
    return {std::round(area.x + (area.width - size.width) * 0.5f), std::round(area.y + (area.height - size.height) * 0.5f),
            size.width, size.height};
}

Rect loadingWindowRect(const DisplayArea& d) {
    Size2 chosen = kLoadingProfiles[0];
    for (const Size2& p : kLoadingProfiles) {
        if (p.width <= d.bounds.width * 0.5f + 0.5f && p.height <= d.bounds.height * 0.5f + 0.5f) {
            chosen = p;
        }
    }
    const Size2 limit{d.usable.width * 0.9f, d.usable.height * 0.9f};
    if (chosen.width > limit.width || chosen.height > limit.height) {
        chosen = fit169(limit, chosen);
    }
    return centeredIn(d.usable, chosen);
}

Rect launcherWindowRect(const DisplayArea& d, float displayScale) {
    const float s = displayScale > 0.0f ? displayScale : 1.0f;
    const Size2 size = fit169({d.usable.width * 0.85f, d.usable.height * 0.85f}, {1920.0f * s, 1080.0f * s});
    return centeredIn(d.usable, size);
}

} // namespace zodar::ui
