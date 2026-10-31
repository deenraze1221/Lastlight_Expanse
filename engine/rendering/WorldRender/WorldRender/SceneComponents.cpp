#include "WorldRender/SceneComponents.h"

#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace zodar {

namespace {

// Entities with component T in entity index order (EnTT storage order is not creation order).
template <typename T>
std::vector<std::pair<Entity, const T*>> sorted(const World& world) {
    std::vector<std::pair<Entity, const T*>> list;
    for (const Entity e : world.entities()) {
        if (const T* c = world.tryGet<T>(e)) {
            list.emplace_back(e, c);
        }
    }
    return list;
}

glm::vec3 forwardOf(const glm::mat4& world) {
    const glm::vec3 f = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    const float length = glm::length(f);
    return length > 0.0f ? f / length : glm::vec3(0.0f, 0.0f, -1.0f);
}

} // namespace

gfx::Projection CameraComponent::projection() const {
    gfx::Projection p;
    p.type = orthographic ? gfx::Projection::Type::Orthographic : gfx::Projection::Type::Perspective;
    p.verticalFovRadians = glm::radians(verticalFovDegrees);
    p.nearPlane = nearPlane;
    p.farPlane = farPlane;
    p.orthographicHeight = orthographicHeight;
    return p;
}

void collectLighting(const World& world, gfx::SceneView& view) {
    gfx::SceneLighting& lighting = view.lighting;
    const auto suns = sorted<DirectionalLightComponent>(world);
    lighting.sun.enabled = !suns.empty();
    if (!suns.empty()) {
        lighting.sun.direction = forwardOf(world.worldMatrix(suns.front().first));
        lighting.sun.color = suns.front().second->color;
        lighting.sun.intensity = suns.front().second->intensity;
    }
    lighting.pointLights.clear();
    for (const auto& [e, c] : sorted<PointLightComponent>(world)) {
        lighting.pointLights.push_back({glm::vec3(world.worldMatrix(e)[3]), c->color, c->intensity, c->range});
    }
    lighting.spotLights.clear();
    for (const auto& [e, c] : sorted<SpotLightComponent>(world)) {
        const glm::mat4 m = world.worldMatrix(e);
        lighting.spotLights.push_back({glm::vec3(m[3]), forwardOf(m), c->color, c->intensity, c->range,
                                       glm::radians(c->innerConeDegrees), glm::radians(c->outerConeDegrees)});
    }
    const auto environments = sorted<EnvironmentComponent>(world);
    if (!environments.empty()) {
        const EnvironmentComponent& env = *environments.front().second;
        view.clearColor = env.clearColor;
        lighting.ambientColor = env.ambientColor;
        lighting.ambientIntensity = env.ambientIntensity;
        lighting.environmentIntensity = env.environmentIntensity;
        lighting.drawEnvironmentBackground = env.drawBackground;
    }
    const auto fogs = sorted<FogComponent>(world);
    lighting.fog.enabled = !fogs.empty();
    if (!fogs.empty()) {
        const FogComponent& f = *fogs.front().second;
        lighting.fog.color = f.color;
        lighting.fog.density = f.density;
        lighting.fog.heightFalloff = f.heightFalloff;
        lighting.fog.baseHeight = f.baseHeight;
        lighting.fog.startDistance = f.startDistance;
        lighting.fog.maxOpacity = f.maxOpacity;
    }
}

bool collectCamera(const World& world, gfx::Camera& camera) {
    for (const auto& [e, c] : sorted<CameraComponent>(world)) {
        if (!c->active) {
            continue;
        }
        const glm::mat4 m = world.worldMatrix(e);
        const glm::mat3 basis(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                              glm::normalize(glm::vec3(m[2])));
        camera.position = glm::vec3(m[3]);
        camera.orientation = glm::normalize(glm::quat_cast(basis));
        camera.projection = c->projection();
        return true;
    }
    return false;
}

} // namespace zodar
