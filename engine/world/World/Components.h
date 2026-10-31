#pragma once

#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace zodar {

// Human-readable entity name (debugging, scene files, reports). Not required to be unique.
struct Name {
    std::string value;
};

// Local transform relative to the parent entity (or to the world when there is no parent).
// Matrix order: translate * rotate * scale. Rotation is a unit quaternion (radians).
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w, x, y, z)
    glm::vec3 scale{1.0f};

    [[nodiscard]] glm::mat4 localMatrix() const noexcept;

    bool operator==(const Transform&) const = default; // Exact comparison (scene round-trips).
};

} // namespace zodar
