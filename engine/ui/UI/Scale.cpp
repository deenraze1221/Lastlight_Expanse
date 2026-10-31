#include "UI/Scale.h"

#include <algorithm>
#include <cmath>

namespace zodar::ui {

float computeScale(ScalePolicy policy, const ScaleInputs& in) {
    float scale = 1.0f;
    if (policy == ScalePolicy::Density) {
        const float display = in.displayScale > 0.0f ? in.displayScale : 1.0f;
        const float user = in.userScale > 0.0f ? in.userScale : 1.0f;
        scale = display * user;
    } else if (in.designSize.width > 0.0f && in.designSize.height > 0.0f) {
        scale = std::min(in.windowPixels.width / in.designSize.width, in.windowPixels.height / in.designSize.height);
        if (in.userScale > 0.0f) scale *= in.userScale;
    }
    if (!std::isfinite(scale)) {
        scale = 1.0f;
    }
    return std::clamp(scale, kMinScale, kMaxScale);
}

float quantizeScale(float scale) {
    return std::round(scale * 20.0f) / 20.0f;
}

Size2 logicalSize(Size2 windowPixels, float scale) {
    const float s = scale > 0.0f ? scale : 1.0f;
    return {windowPixels.width / s, windowPixels.height / s};
}

} // namespace zodar::ui
