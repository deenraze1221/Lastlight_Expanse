#include "Renderer/Frustum.h"

#include <cmath>

namespace raven::gfx {

Frustum::Frustum(const glm::mat4& m) {
    const glm::vec4 row0{m[0][0], m[1][0], m[2][0], m[3][0]};
    const glm::vec4 row1{m[0][1], m[1][1], m[2][1], m[3][1]};
    const glm::vec4 row2{m[0][2], m[1][2], m[2][2], m[3][2]};
    const glm::vec4 row3{m[0][3], m[1][3], m[2][3], m[3][3]};
    // Clip space: -w <= x,y <= w and 0 <= z <= w; reverse Z puts the near plane at z = w.
    const glm::vec4 candidates[6] = {row3 + row0, row3 - row0, row3 + row1, row3 - row1,
                                     row3 - row2,  // near (z <= w)
                                     row2};        // far (z >= 0); degenerate for infinite projections
    for (const glm::vec4& p : candidates) {
        const float len = glm::length(glm::vec3(p));
        if (len > 1e-6f) {
            m_planes[m_planeCount++] = p / len;
        }
    }
}

bool Frustum::intersects(const Bounds& b) const {
    if (b.isEmpty()) {
        return false;
    }
    for (int i = 0; i < m_planeCount; ++i) {
        const glm::vec3 n(m_planes[i]);
        // Corner of the box furthest along the plane normal.
        const glm::vec3 positive{n.x >= 0 ? b.max.x : b.min.x, n.y >= 0 ? b.max.y : b.min.y,
                                 n.z >= 0 ? b.max.z : b.min.z};
        if (glm::dot(n, positive) + m_planes[i].w < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace raven::gfx
