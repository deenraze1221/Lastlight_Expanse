#pragma once

#include "World/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

// Component reflection (Phase B2): static field tables describing component types, so scene
// serialization, documentation (docs/guide/SCENE_COMPONENTS.md is generated from them) and the
// future console share one description. A component is reflected by specialising
// ComponentReflection<T> with `key` (JSON / console name), `description` and `fields()`.
namespace zodar {

enum class FieldType : uint32_t { Bool, Uint, Float, Vec3, Vec4, Quat, String };

// Field type -> C++ type: Bool bool, Uint uint32_t, Float float, Vec3 glm::vec3, Vec4 glm::vec4,
// Quat glm::quat, String std::string.
struct FieldInfo {
    std::string_view name;
    FieldType type = FieldType::Float;
    void* (*access)(void* component) = nullptr; // Address of the field inside a component.
    std::string_view description;
    float min = std::numeric_limits<float>::lowest(); // Inclusive range, per component for vectors.
    float max = std::numeric_limits<float>::max();

    template <typename V>
    [[nodiscard]] V& get(void* component) const {
        return *static_cast<V*>(access(component));
    }
    template <typename V>
    [[nodiscard]] const V& get(const void* component) const {
        return *static_cast<const V*>(access(const_cast<void*>(component)));
    }
};

template <typename T>
struct ComponentReflection; // Specialise per component type.

template <typename T>
concept Reflected = requires {
    { ComponentReflection<T>::key } -> std::convertible_to<std::string_view>;
    { ComponentReflection<T>::description } -> std::convertible_to<std::string_view>;
    { ComponentReflection<T>::fields() } -> std::convertible_to<std::span<const FieldInfo>>;
};

// Type-erased description of a reflected component type.
struct ComponentInfo {
    std::string_view key;
    std::string_view description;
    std::span<const FieldInfo> fields;
};

template <Reflected T>
[[nodiscard]] ComponentInfo componentInfo() {
    return {ComponentReflection<T>::key, ComponentReflection<T>::description, ComponentReflection<T>::fields()};
}

// Human-readable value of a field ("[1, 0.5, 0]", "true", "\"name\""), as used in docs.
[[nodiscard]] std::string formatField(const FieldInfo& field, const void* component);

[[nodiscard]] std::string_view fieldTypeName(FieldType type);

// Field accessor helper: ZODAR_FIELD_ACCESS(PointLight, intensity).
#define ZODAR_FIELD_ACCESS(Type, member) \
    [](void* component) -> void* { return &static_cast<Type*>(component)->member; }

template <>
struct ComponentReflection<Name> {
    static constexpr std::string_view key = "name";
    static constexpr std::string_view description = "Human-readable entity name (not unique).";
    static std::span<const FieldInfo> fields() {
        static constexpr FieldInfo table[] = {
            {"value", FieldType::String, ZODAR_FIELD_ACCESS(Name, value), "Name text"},
        };
        return table;
    }
};

template <>
struct ComponentReflection<Transform> {
    static constexpr std::string_view key = "transform";
    static constexpr std::string_view description =
        "Local transform relative to the parent (translate * rotate * scale).";
    static std::span<const FieldInfo> fields() {
        static constexpr FieldInfo table[] = {
            {"position", FieldType::Vec3, ZODAR_FIELD_ACCESS(Transform, position), "Local position (world units)"},
            {"rotation", FieldType::Quat, ZODAR_FIELD_ACCESS(Transform, rotation),
             "Unit quaternion, written [x, y, z, w]"},
            {"scale", FieldType::Vec3, ZODAR_FIELD_ACCESS(Transform, scale), "Local scale"},
        };
        return table;
    }
};

} // namespace zodar
