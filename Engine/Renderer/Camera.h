#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace zodar::gfx {

// Engine conventions: right-handed world, +Y up, a camera looks along its local -Z.
// Projections target Vulkan clip space (Y down, depth 0..1) with **reverse Z**:
// the near plane maps to depth 1 and the far plane (or infinity) to depth 0.
// Depth tests therefore use GREATER_OR_EQUAL and depth is cleared to 0.
struct Projection {
    enum class Type { Perspective, Orthographic };
    Type type = Type::Perspective;
    float verticalFovRadians = glm::radians(60.0f);
    float nearPlane = 0.05f;
    float farPlane = 0.0f;           // 0 = infinite far plane (perspective only).
    float orthographicHeight = 10.0f; // World units covered vertically.
};

glm::mat4 perspectiveReverseZ(float verticalFov, float aspect, float nearPlane, float farPlane);
glm::mat4 orthographicReverseZ(float height, float aspect, float nearPlane, float farPlane);

class Camera {
public:
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    Projection projection;

    glm::vec3 forward() const { return orientation * glm::vec3(0.0f, 0.0f, -1.0f); }
    glm::vec3 right() const { return orientation * glm::vec3(1.0f, 0.0f, 0.0f); }
    glm::vec3 up() const { return orientation * glm::vec3(0.0f, 1.0f, 0.0f); }

    // Orients the camera toward a point. `worldUp` must not be parallel to the view direction.
    void lookAt(const glm::vec3& target, const glm::vec3& worldUp = {0.0f, 1.0f, 0.0f});
    // Yaw around world +Y, then pitch around the local X axis (radians).
    void setYawPitch(float yaw, float pitch);

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
};

} // namespace zodar::gfx
