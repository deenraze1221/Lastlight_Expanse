#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cstdint>

namespace zodar::gfx {

// GPU layout of one instance (std430). Mirrors ZodarInstance in zodar/scene.glsl.
// Shared by the CPU draw path and GpuScene so both produce bit-identical instance data.
struct GpuInstance {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::uvec4 data; // x: material index
};
static_assert(sizeof(GpuInstance) == 144, "must match ZodarInstance in zodar/scene.glsl");

inline GpuInstance makeGpuInstance(const glm::mat4& model, uint32_t material) {
    return {model, glm::mat4(glm::inverseTranspose(glm::mat3(model))), glm::uvec4(material, 0, 0, 0)};
}

} // namespace zodar::gfx
