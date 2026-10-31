#pragma once

#include "Assets/Bounds.h"

#include <glm/glm.hpp>

namespace zodar::gfx {

// View frustum planes extracted from a reverse-Z view-projection matrix (Vulkan clip
// space). Planes point inwards; an infinite far plane is detected and skipped.
class Frustum {
public:
    explicit Frustum(const glm::mat4& viewProjection);

    // Conservative test: false only when the box is entirely outside one plane.
    bool intersects(const Bounds& worldBounds) const;

    // Normalised inward planes (xyz normal, w distance); count is 5 for infinite projections.
    const glm::vec4* planes() const { return m_planes; }
    int planeCount() const { return m_planeCount; }

private:
    glm::vec4 m_planes[6];
    int m_planeCount = 0;
};

} // namespace zodar::gfx
