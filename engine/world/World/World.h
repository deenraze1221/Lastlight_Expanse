#pragma once

#include "World/Components.h"
#include "World/Entity.h"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

#include <glm/glm.hpp>

namespace zodar {

namespace detail {
// Parent/child links, managed only by World (setParent/destroy). Children keep insertion order.
struct HierarchyNode {
    Entity parent;
    std::vector<Entity> children;
};
} // namespace detail

// Owns entities and their components. Thin wrapper over entt::registry: callers use only this
// API so the backend stays replaceable. Not thread-safe; use from one thread (the main/update
// thread). Non-copyable, movable.
//
// Components are plain value types (one instance of each type per entity). References and
// pointers returned by add/get/tryGet are invalidated by any later add/remove of the same
// component type and by destroy/clear.
class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;
    ~World() = default;

    // Creates a new live entity.
    [[nodiscard]] Entity create();

    // Destroys a live entity and, recursively, all of its children; detaches it from its
    // parent. Returns false (and does nothing) for null, already destroyed or foreign handles.
    bool destroy(Entity entity);

    [[nodiscard]] bool isAlive(Entity entity) const noexcept;

    // Number of live entities.
    [[nodiscard]] std::size_t entityCount() const noexcept;

    // Destroys every entity. Previously returned handles become invalid.
    void clear();

    // --- Components ---------------------------------------------------------------------

    // Adds (or replaces) component T on a live entity. Throws std::invalid_argument for a dead
    // entity.
    template <typename T, typename... Args>
    T& add(Entity entity, Args&&... args) {
        static_assert(!std::is_same_v<T, detail::HierarchyNode>, "hierarchy is managed by World");
        requireAlive(entity);
        return m_registry.emplace_or_replace<T>(toEntt(entity), std::forward<Args>(args)...);
    }

    // Component T of a live entity. Throws std::out_of_range if the entity is dead or has no T.
    template <typename T>
    [[nodiscard]] T& get(Entity entity) {
        T* component = tryGet<T>(entity);
        if (component == nullptr) {
            throw std::out_of_range("zodar::World::get: entity has no such component");
        }
        return *component;
    }
    template <typename T>
    [[nodiscard]] const T& get(Entity entity) const {
        const T* component = tryGet<T>(entity);
        if (component == nullptr) {
            throw std::out_of_range("zodar::World::get: entity has no such component");
        }
        return *component;
    }

    // Component T, or nullptr if the entity is dead or has no T.
    template <typename T>
    [[nodiscard]] T* tryGet(Entity entity) noexcept {
        return isAlive(entity) ? m_registry.try_get<T>(toEntt(entity)) : nullptr;
    }
    template <typename T>
    [[nodiscard]] const T* tryGet(Entity entity) const noexcept {
        return isAlive(entity) ? m_registry.try_get<T>(toEntt(entity)) : nullptr;
    }

    template <typename T>
    [[nodiscard]] bool has(Entity entity) const noexcept {
        return tryGet<T>(entity) != nullptr;
    }

    // Removes component T. Returns false if the entity is dead or had no T.
    template <typename T>
    bool remove(Entity entity) {
        static_assert(!std::is_same_v<T, detail::HierarchyNode>, "hierarchy is managed by World");
        return isAlive(entity) && m_registry.remove<T>(toEntt(entity)) > 0;
    }

    // --- Hierarchy ----------------------------------------------------------------------

    // Makes `parent` the parent of `child` (appended as its last child). Entity::null()
    // detaches `child` (makes it a root). Returns false and changes nothing if `child` is dead,
    // `parent` is non-null and dead, or the link would create a cycle (parent == child or
    // parent is a descendant of child). Setting the current parent again is a no-op (true).
    bool setParent(Entity child, Entity parent);

    // Parent of a live entity, or Entity::null() for roots and dead entities.
    [[nodiscard]] Entity parent(Entity entity) const noexcept;

    // Direct children in insertion order; empty for dead entities. Invalidated by hierarchy
    // changes, destroy and clear.
    [[nodiscard]] std::span<const Entity> children(Entity entity) const noexcept;

    // True if `ancestor` is a (direct or indirect) parent of `entity`.
    [[nodiscard]] bool isDescendantOf(Entity entity, Entity ancestor) const noexcept;

    // --- Transforms ---------------------------------------------------------------------

    // Local matrix of the entity's Transform; identity if it has none (or is dead).
    [[nodiscard]] glm::mat4 localMatrix(Entity entity) const noexcept;

    // parent world matrix * local matrix, up to the root. Entities without a Transform
    // contribute identity. Computed on demand (O(depth)); identity for dead entities.
    [[nodiscard]] glm::mat4 worldMatrix(Entity entity) const noexcept;

private:
    [[nodiscard]] static entt::entity toEntt(Entity entity) noexcept {
        return static_cast<entt::entity>(entity.id());
    }
    void requireAlive(Entity entity) const {
        if (!isAlive(entity)) {
            throw std::invalid_argument("zodar::World: entity is not alive");
        }
    }
    void detachFromParent(Entity entity);

    entt::registry m_registry;
};

} // namespace zodar
