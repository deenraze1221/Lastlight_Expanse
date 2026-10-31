#include "Renderer/Lighting/ClusterMath.h"

#include <algorithm>
#include <cmath>

namespace zodar::gfx {

ClusterConfig clusterConfig(LightingQuality quality) {
    switch (quality) {
    case LightingQuality::Low: return {128, 16, 128};
    case LightingQuality::Medium: return {64, 24, 256};
    case LightingQuality::High: return {64, 32, 512};
    case LightingQuality::Ultra: return {32, 32, 256};
    }
    return {};
}

const char* toString(LightingQuality quality) {
    switch (quality) {
    case LightingQuality::Low: return "low";
    case LightingQuality::Medium: return "medium";
    case LightingQuality::High: return "high";
    case LightingQuality::Ultra: return "ultra";
    }
    return "?";
}

ClusterGrid makeClusterGrid(uint32_t width, uint32_t height, const ClusterConfig& config, float nearPlane,
                            float farPlane) {
    ClusterGrid g;
    // Tile sizes are defined for 1080p; larger outputs scale them so the cluster count (and the
    // light-list memory) stays roughly constant across resolutions.
    const float scale = std::max(1.0f, float(height) / 1080.0f);
    g.tileSize = std::max(8u, (static_cast<uint32_t>(std::lround(float(config.tileSize) * scale)) + 7u) / 8u * 8u);
    g.x = std::max(1u, (width + g.tileSize - 1) / g.tileSize);
    g.y = std::max(1u, (height + g.tileSize - 1) / g.tileSize);
    g.z = std::max(config.slices, 1u);
    g.nearPlane = std::max(nearPlane, 1e-4f);
    g.farPlane = std::max(farPlane, g.nearPlane * 2.0f);
    const float logRatio = std::log(g.farPlane / g.nearPlane);
    g.sliceScale = float(g.z) / logRatio;
    g.sliceBias = -float(g.z) * std::log(g.nearPlane) / logRatio;
    return g;
}

uint32_t clusterSlice(const ClusterGrid& grid, float depth) {
    const float s = std::floor(std::log(std::max(depth, 1e-6f)) * grid.sliceScale + grid.sliceBias);
    return static_cast<uint32_t>(std::clamp(s, 0.0f, float(grid.z - 1)));
}

float sliceDepth(const ClusterGrid& grid, uint32_t slice) {
    return grid.nearPlane * std::pow(grid.farPlane / grid.nearPlane, float(slice) / float(grid.z));
}

float effectiveLightRange(float intensity, const glm::vec3& color, float range, float cutoff) {
    if (range > 0.0f) {
        return range;
    }
    const float peak = intensity * std::max({color.r, color.g, color.b, 0.0f});
    return std::sqrt(std::max(peak, 0.0f) / std::max(cutoff, 1e-8f));
}

glm::vec2 spotScaleOffset(float innerConeRadians, float outerConeRadians) {
    const float outer = std::clamp(outerConeRadians, 0.0f, 1.5707f);
    const float inner = std::clamp(innerConeRadians, 0.0f, outer);
    const float cosOuter = std::cos(outer);
    const float scale = 1.0f / std::max(std::cos(inner) - cosOuter, 1e-4f);
    return {scale, -cosOuter * scale};
}

} // namespace zodar::gfx
