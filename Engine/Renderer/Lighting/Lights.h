#pragma once

#include "Renderer/Lighting/ClusterMath.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace zodar::gfx {

class Environment;

// Light units: colour * intensity is the radiant intensity (per steradian) of point and spot
// lights, so irradiance at distance d is color * intensity / d^2 (no 1/(4 pi) normalisation;
// spot lights keep the same intensity inside the cone instead of focusing the flux).
// Directional lights give irradiance color * intensity directly.

struct DirectionalLight {
    glm::vec3 direction{-0.4f, -1.0f, -0.3f}; // Direction the light travels.
    glm::vec3 color{1.0f};
    float intensity = 3.0f;
    bool enabled = true;
};

struct PointLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 10.0f; // Inverse-square: irradiance = intensity / d^2.
    // Smooth cut-off distance; 0 = automatic (where irradiance falls to
    // LightingSettings::lightCutoff). Lights only affect fragments within their range.
    float range = 0.0f;
};

struct SpotLight {
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // Direction the light points to.
    glm::vec3 color{1.0f};
    float intensity = 10.0f;
    float range = 0.0f;
    float innerConeRadians = 0.35f; // Full intensity inside
    float outerConeRadians = 0.55f; // Zero outside (max ~89.9 degrees)
};

struct SceneLighting {
    // Constant ambient radiance, used only when no environment is set.
    glm::vec3 ambientColor{1.0f};
    float ambientIntensity = 0.03f;
    DirectionalLight sun;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;
    // Image-based lighting (baked by IblBaker); nullptr = constant ambient.
    const Environment* environment = nullptr;
    float environmentIntensity = 1.0f;
    bool drawEnvironmentBackground = true; // Sky where no geometry was drawn (else clear colour)
};

enum class LightingDebugView : uint32_t {
    None = 0,
    LightCount = 1,   // Heat map: lights in each fragment's cluster (red = 32+)
    ClusterSlice = 2, // Heat map: depth slice
    Normals = 3,
    Direct = 4,
    Indirect = 5,
    SpecularIbl = 6,
};
const char* toString(LightingDebugView view);
// Parse the names printed by toString(); throw zodar::Error listing the valid names.
LightingDebugView parseLightingDebugView(const std::string& name);
LightingQuality parseLightingQuality(const std::string& name);

// Renderer-wide lighting configuration. Changing the quality reallocates cluster buffers.
struct LightingSettings {
    LightingQuality quality = LightingQuality::High;
    float clusterFar = 500.0f;     // Depth covered by exponential slices (beyond: last slice)
    float lightCutoff = 0.02f;     // Irradiance defining automatic light ranges
    uint32_t maxLights = 65535;    // 16-bit light indices
    bool multiscatter = true;      // GGX multiple-scattering energy compensation
    bool depthPrepass = true;      // Opaque/masked depth-only pass before shading
};

} // namespace zodar::gfx
