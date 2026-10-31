#pragma once

#include "Renderer/Camera.h"
#include "Renderer/SceneRenderer.h"
#include "World/Reflection.h"
#include "World/World.h"

#include <glm/glm.hpp>

// Camera, light, environment and fog components (Phase B2). Position and direction come from
// the entity's world transform: lights and cameras point along the entity's local -Z.
// collectLighting / collectCamera turn them into gfx::SceneView data. Reflected (field tables
// below), so scene files and docs (docs/guide/SCENE_COMPONENTS.md) use the same description.
namespace zodar {

struct CameraComponent {
    bool orthographic = false;
    float verticalFovDegrees = 60.0f;
    float nearPlane = 0.05f;
    float farPlane = 0.0f; // 0 = infinite (perspective only).
    float orthographicHeight = 10.0f;
    bool active = true;

    bool operator==(const CameraComponent&) const = default;
    [[nodiscard]] gfx::Projection projection() const;
};

struct DirectionalLightComponent {
    glm::vec3 color{1.0f};
    float intensity = 3.0f;

    bool operator==(const DirectionalLightComponent&) const = default;
};

struct PointLightComponent {
    glm::vec3 color{1.0f};
    float intensity = 10.0f;
    float range = 0.0f;

    bool operator==(const PointLightComponent&) const = default;
};

struct SpotLightComponent {
    glm::vec3 color{1.0f};
    float intensity = 10.0f;
    float range = 0.0f;
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 31.5f;

    bool operator==(const SpotLightComponent&) const = default;
};

// Ambient light, clear colour and IBL intensity. The IBL environment map itself is an asset
// and stays set in code (SceneLighting::environment) until the Phase C asset manager.
struct EnvironmentComponent {
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec3 ambientColor{1.0f};
    float ambientIntensity = 0.03f;
    float environmentIntensity = 1.0f;
    bool drawBackground = true;

    bool operator==(const EnvironmentComponent&) const = default;
};

// Presence enables fog (gfx::FogSettings).
struct FogComponent {
    glm::vec3 color{0.5f, 0.6f, 0.7f};
    float density = 0.01f;
    float heightFalloff = 0.0f;
    float baseHeight = 0.0f;
    float startDistance = 0.0f;
    float maxOpacity = 1.0f;

    bool operator==(const FogComponent&) const = default;
};

// Fills view.lighting and view.clearColor from the world. Always replaces the sun (first
// DirectionalLightComponent by entity index; disabled when there is none), point and spot
// lights (entity index order) and fog (first FogComponent; disabled when there is none).
// Ambient, clear colour, environment intensity and background come from the first
// EnvironmentComponent and are left unchanged when there is none. The IBL environment
// pointer is never touched.
void collectLighting(const World& world, gfx::SceneView& view);

// Copies the first active CameraComponent (by entity index) into `camera`: position and
// orientation from the world matrix (scale removed), projection from the component.
// Returns false (camera unchanged) if the world has no active camera.
bool collectCamera(const World& world, gfx::Camera& camera);

// --- Reflection -------------------------------------------------------------------------

template <>
struct ComponentReflection<CameraComponent> {
    static constexpr std::string_view key = "camera";
    static constexpr std::string_view description = "Camera looking along the entity's -Z axis.";
    static std::span<const FieldInfo> fields() {
        using T = CameraComponent;
        static constexpr FieldInfo table[] = {
            {"orthographic", FieldType::Bool, ZODAR_FIELD_ACCESS(T, orthographic), "Orthographic instead of perspective"},
            {"verticalFovDegrees", FieldType::Float, ZODAR_FIELD_ACCESS(T, verticalFovDegrees),
             "Perspective vertical field of view (degrees)", 1.0f, 179.0f},
            {"nearPlane", FieldType::Float, ZODAR_FIELD_ACCESS(T, nearPlane), "Near plane distance", 1e-6f},
            {"farPlane", FieldType::Float, ZODAR_FIELD_ACCESS(T, farPlane), "Far plane distance; 0 = infinite", 0.0f},
            {"orthographicHeight", FieldType::Float, ZODAR_FIELD_ACCESS(T, orthographicHeight),
             "World units covered vertically (orthographic)", 1e-6f},
            {"active", FieldType::Bool, ZODAR_FIELD_ACCESS(T, active), "Considered by collectCamera"},
        };
        return table;
    }
};

template <>
struct ComponentReflection<DirectionalLightComponent> {
    static constexpr std::string_view key = "directionalLight";
    static constexpr std::string_view description = "Sun light travelling along the entity's -Z axis.";
    static std::span<const FieldInfo> fields() {
        using T = DirectionalLightComponent;
        static constexpr FieldInfo table[] = {
            {"color", FieldType::Vec3, ZODAR_FIELD_ACCESS(T, color), "Linear colour", 0.0f},
            {"intensity", FieldType::Float, ZODAR_FIELD_ACCESS(T, intensity), "Irradiance multiplier", 0.0f},
        };
        return table;
    }
};

template <>
struct ComponentReflection<PointLightComponent> {
    static constexpr std::string_view key = "pointLight";
    static constexpr std::string_view description = "Point light at the entity's world position.";
    static std::span<const FieldInfo> fields() {
        using T = PointLightComponent;
        static constexpr FieldInfo table[] = {
            {"color", FieldType::Vec3, ZODAR_FIELD_ACCESS(T, color), "Linear colour", 0.0f},
            {"intensity", FieldType::Float, ZODAR_FIELD_ACCESS(T, intensity), "Radiant intensity (inverse-square)",
             0.0f},
            {"range", FieldType::Float, ZODAR_FIELD_ACCESS(T, range), "Cut-off distance; 0 = automatic", 0.0f},
        };
        return table;
    }
};

template <>
struct ComponentReflection<SpotLightComponent> {
    static constexpr std::string_view key = "spotLight";
    static constexpr std::string_view description = "Spot light pointing along the entity's -Z axis.";
    static std::span<const FieldInfo> fields() {
        using T = SpotLightComponent;
        static constexpr FieldInfo table[] = {
            {"color", FieldType::Vec3, ZODAR_FIELD_ACCESS(T, color), "Linear colour", 0.0f},
            {"intensity", FieldType::Float, ZODAR_FIELD_ACCESS(T, intensity), "Radiant intensity (inverse-square)",
             0.0f},
            {"range", FieldType::Float, ZODAR_FIELD_ACCESS(T, range), "Cut-off distance; 0 = automatic", 0.0f},
            {"innerConeDegrees", FieldType::Float, ZODAR_FIELD_ACCESS(T, innerConeDegrees),
             "Full intensity inside (half angle, degrees)", 0.0f, 89.9f},
            {"outerConeDegrees", FieldType::Float, ZODAR_FIELD_ACCESS(T, outerConeDegrees),
             "Zero outside (half angle, degrees)", 0.0f, 89.9f},
        };
        return table;
    }
};

template <>
struct ComponentReflection<EnvironmentComponent> {
    static constexpr std::string_view key = "environment";
    static constexpr std::string_view description = "Ambient light, clear colour and IBL intensity.";
    static std::span<const FieldInfo> fields() {
        using T = EnvironmentComponent;
        static constexpr FieldInfo table[] = {
            {"clearColor", FieldType::Vec4, ZODAR_FIELD_ACCESS(T, clearColor), "Linear HDR clear colour", 0.0f},
            {"ambientColor", FieldType::Vec3, ZODAR_FIELD_ACCESS(T, ambientColor),
             "Constant ambient (used without an IBL environment)", 0.0f},
            {"ambientIntensity", FieldType::Float, ZODAR_FIELD_ACCESS(T, ambientIntensity), "Ambient multiplier", 0.0f},
            {"environmentIntensity", FieldType::Float, ZODAR_FIELD_ACCESS(T, environmentIntensity),
             "IBL multiplier", 0.0f},
            {"drawBackground", FieldType::Bool, ZODAR_FIELD_ACCESS(T, drawBackground),
             "Draw the IBL sky where no geometry was drawn"},
        };
        return table;
    }
};

template <>
struct ComponentReflection<FogComponent> {
    static constexpr std::string_view key = "fog";
    static constexpr std::string_view description = "Distance + exponential height fog (presence enables it).";
    static std::span<const FieldInfo> fields() {
        using T = FogComponent;
        static constexpr FieldInfo table[] = {
            {"color", FieldType::Vec3, ZODAR_FIELD_ACCESS(T, color), "Linear colour", 0.0f},
            {"density", FieldType::Float, ZODAR_FIELD_ACCESS(T, density), "Extinction per world unit at baseHeight",
             0.0f},
            {"heightFalloff", FieldType::Float, ZODAR_FIELD_ACCESS(T, heightFalloff), "0 = uniform", 0.0f},
            {"baseHeight", FieldType::Float, ZODAR_FIELD_ACCESS(T, baseHeight), "Reference height"},
            {"startDistance", FieldType::Float, ZODAR_FIELD_ACCESS(T, startDistance), "No fog closer than this", 0.0f},
            {"maxOpacity", FieldType::Float, ZODAR_FIELD_ACCESS(T, maxOpacity), "Opacity clamp", 0.0f, 1.0f},
        };
        return table;
    }
};

} // namespace zodar
