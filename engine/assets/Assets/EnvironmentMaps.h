#pragma once

#include "Assets/ImageData.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace zodar {

// Equirectangular (2:1, +Y at the top row, u = atan(z, x) / 2pi + 0.5) linear HDR images used
// as environment-lighting sources when no .hdr file is supplied, and for tests.

struct ProceduralSkyDesc {
    glm::vec3 zenithColor{0.25f, 0.45f, 0.9f};
    glm::vec3 horizonColor{0.75f, 0.85f, 1.0f};
    glm::vec3 groundColor{0.2f, 0.18f, 0.16f};
    float skyIntensity = 1.0f;
    glm::vec3 sunDirection{-0.4f, -1.0f, -0.3f}; // Direction the sunlight travels
    glm::vec3 sunColor{1.0f, 0.95f, 0.85f};
    float sunIntensity = 50.0f;   // Radiance of the sun disk (kept moderate for 16-bit floats)
    float sunAngularRadius = 0.03f; // Radians
};
ImageData proceduralSky(uint32_t width, const ProceduralSkyDesc& desc = {});

// Constant radiance in every direction (white-furnace tests).
ImageData uniformEnvironment(const glm::vec3& radiance, uint32_t width = 64);

} // namespace zodar
