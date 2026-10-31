#include "UI/Widgets/ProgressBar.h"

#include <algorithm>
#include <cmath>

namespace zodar::ui {

float smoothProgress(float displayed, float target, float dt) {
    target = std::clamp(std::isfinite(target) ? target : 0.0f, 0.0f, 1.0f);
    if (!std::isfinite(displayed) || target < displayed) {
        return target;
    }
    const float k = 1.0f - std::exp(-8.0f * std::max(0.0f, dt)); // ~95% of the gap in 0.37 s.
    const float next = displayed + (target - displayed) * k;
    return target - next < 0.0005f ? target : next;
}

Rect indeterminateSegment(Rect track, double t) {
    const double phase = std::fmod(std::max(0.0, t), kIndeterminatePeriod) / kIndeterminatePeriod;
    // Ease in-out (cosine), from fully left of the track to fully right of it.
    const float eased = static_cast<float>(0.5 - 0.5 * std::cos(phase * 3.14159265358979));
    const float w = track.width * kIndeterminateWidth;
    const float x = track.x - w + (track.width + w) * eased;
    return {x, track.y, w, track.height};
}

void progressBar(Context& ctx, Rect logical, std::optional<float> fraction, double t, Color fill, Color trackColor) {
    DrawList& dl = ctx.draw();
    const Rect px = ctx.toPixels(logical);
    // Keep the bar at least 1 px tall at small scales.
    const Rect bar{px.x, px.y, px.width, std::max(1.0f, px.height)};
    dl.fillRect(bar, trackColor);
    if (fraction) {
        const float f = std::clamp(*fraction, 0.0f, 1.0f);
        dl.fillRect({bar.x, bar.y, std::round(bar.width * f), bar.height}, fill);
        return;
    }
    dl.pushClip(bar);
    const Rect seg = indeterminateSegment(bar, t);
    dl.fillRect({std::round(seg.x), seg.y, std::round(seg.width), seg.height}, fill);
    dl.popClip();
}

} // namespace zodar::ui
