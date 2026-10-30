#pragma once

#include <glm/glm.hpp>

#include <limits>

namespace raven {

// Axis-aligned bounding box. Default-constructed boxes are empty.
struct Bounds {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    bool isEmpty() const { return min.x > max.x; }
    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void expand(const Bounds& b) {
        if (!b.isEmpty()) {
            expand(b.min);
            expand(b.max);
        }
    }
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extent() const { return max - min; }

    // Bounds of this box after an affine transform (all eight corners).
    Bounds transformed(const glm::mat4& m) const {
        Bounds out;
        if (isEmpty()) {
            return out;
        }
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner{(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
            out.expand(glm::vec3(m * glm::vec4(corner, 1.0f)));
        }
        return out;
    }
};

} // namespace raven
