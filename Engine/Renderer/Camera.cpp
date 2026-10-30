#include "Renderer/Camera.h"

#include "Core/Error.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace raven::gfx {

glm::mat4 perspectiveReverseZ(float verticalFov, float aspect, float nearPlane, float farPlane) {
    RAVEN_ENSURE(aspect > 0.0f && nearPlane > 0.0f && verticalFov > 0.0f, "invalid perspective parameters");
    const float f = 1.0f / std::tan(verticalFov * 0.5f);
    glm::mat4 m(0.0f);
    m[0][0] = f / aspect;
    m[1][1] = -f; // Vulkan clip space has +Y pointing down.
    m[2][3] = -1.0f;
    if (farPlane <= 0.0f) {
        m[3][2] = nearPlane; // depth = near / -z_view
    } else {
        RAVEN_ENSURE(farPlane > nearPlane, "far plane must be beyond the near plane");
        m[2][2] = nearPlane / (farPlane - nearPlane);
        m[3][2] = farPlane * nearPlane / (farPlane - nearPlane);
    }
    return m;
}

glm::mat4 orthographicReverseZ(float height, float aspect, float nearPlane, float farPlane) {
    RAVEN_ENSURE(height > 0.0f && aspect > 0.0f && farPlane > nearPlane, "invalid orthographic parameters");
    const float halfH = height * 0.5f;
    const float halfW = halfH * aspect;
    glm::mat4 m(1.0f);
    m[0][0] = 1.0f / halfW;
    m[1][1] = -1.0f / halfH;
    m[2][2] = 1.0f / (farPlane - nearPlane);   // depth = (z_view + far) / (far - near)
    m[3][2] = farPlane / (farPlane - nearPlane);
    return m;
}

void Camera::lookAt(const glm::vec3& target, const glm::vec3& worldUp) {
    const glm::vec3 dir = target - position;
    RAVEN_ENSURE(glm::dot(dir, dir) > 1e-12f, "lookAt target equals camera position");
    orientation = glm::quatLookAtRH(glm::normalize(dir), worldUp);
}

void Camera::setYawPitch(float yaw, float pitch) {
    orientation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::mat4 Camera::viewMatrix() const {
    const glm::mat4 rotation = glm::mat4_cast(glm::conjugate(orientation));
    return rotation * glm::translate(glm::mat4(1.0f), -position);
}

glm::mat4 Camera::projectionMatrix(float aspect) const {
    if (projection.type == Projection::Type::Orthographic) {
        const float farPlane = projection.farPlane > 0.0f ? projection.farPlane : 1000.0f;
        return orthographicReverseZ(projection.orthographicHeight, aspect, projection.nearPlane, farPlane);
    }
    return perspectiveReverseZ(projection.verticalFovRadians, aspect, projection.nearPlane, projection.farPlane);
}

} // namespace raven::gfx
