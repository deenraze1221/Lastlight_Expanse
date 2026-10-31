#pragma once

#include "World/Entity.h"

#include <cstddef>

#include <entt/entity/registry.hpp>

namespace zodar {

// Owns entities (and, from Phase B1.2, their components). Thin wrapper over entt::registry:
// callers use only this API so the backend stays replaceable. Not thread-safe; use from one
// thread (the main/update thread). Non-copyable, movable.
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

    // Destroys a live entity. Returns false (and does nothing) for null, already destroyed or
    // foreign handles.
    bool destroy(Entity entity);

    [[nodiscard]] bool isAlive(Entity entity) const noexcept;

    // Number of live entities.
    [[nodiscard]] std::size_t entityCount() const noexcept;

    // Destroys every entity. Previously returned handles become invalid.
    void clear();

private:
    entt::registry m_registry;
};

} // namespace zodar
