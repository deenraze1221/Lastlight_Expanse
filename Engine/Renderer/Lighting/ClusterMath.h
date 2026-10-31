#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace zodar::gfx {

// Clustered Forward+ (Olsson et al. 2012; exponential depth slicing as in Doom 2016).
// The view frustum is split into tileSize x tileSize pixel tiles and `slices` depth slices
// spaced exponentially between `nearPlane` and `farPlane`. A compute pass lists the lights
// overlapping every cluster; fragments only evaluate their cluster's list.
// Everything here is CPU-side and pure (unit tested); Shaders/Include/zodar/clusters.glsl
// implements the same functions.

enum class LightingQuality : uint8_t { Low, Medium, High, Ultra };

struct ClusterConfig {
    uint32_t tileSize = 64;             // Pixels per tile edge at 1080p (scaled with the output height)
    uint32_t slices = 32;               // Depth slices
    uint32_t maxLightsPerCluster = 512; // Fixed-capacity list per cluster (overflow is counted)
};
ClusterConfig clusterConfig(LightingQuality quality);
const char* toString(LightingQuality quality);

struct ClusterGrid {
    uint32_t x = 1, y = 1, z = 1;
    uint32_t tileSize = 64;
    float nearPlane = 0.05f; // View depths (positive distances along the view direction)
    float farPlane = 500.0f;
    float sliceScale = 0.0f; // slice = log(depth) * sliceScale + sliceBias
    float sliceBias = 0.0f;
    uint32_t count() const { return x * y * z; }
};

// `farPlane`: depth covered by the slices; fragments beyond it use the last slice.
ClusterGrid makeClusterGrid(uint32_t width, uint32_t height, const ClusterConfig& config, float nearPlane,
                            float farPlane);
// Slice of a fragment at view depth `depth` (clamped to [0, z-1]).
uint32_t clusterSlice(const ClusterGrid& grid, float depth);
// View depth where `slice` begins (slice == z gives farPlane).
float sliceDepth(const ClusterGrid& grid, uint32_t slice);

// Range used for culling and for the smooth distance window. Lights with range <= 0 get the
// distance where their irradiance (intensity * max colour channel / d^2) falls to `cutoff`.
float effectiveLightRange(float intensity, const glm::vec3& color, float range, float cutoff);

// Angular falloff of spot lights (Filament): attenuation = saturate(cd * scale + offset)^2 with
// cd = cos(angle to the axis). Returns {scale, offset}.
glm::vec2 spotScaleOffset(float innerConeRadians, float outerConeRadians);

} // namespace zodar::gfx
