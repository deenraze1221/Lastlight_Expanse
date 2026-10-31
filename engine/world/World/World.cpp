#include "World/World.h"

#include <type_traits>

namespace zodar {

static_assert(std::is_same_v<std::underlying_type_t<entt::entity>, Entity::Id>,
              "Entity::Id must match the EnTT entity type");
static_assert(static_cast<Entity::Id>(entt::entity{entt::null}) == Entity::kNullId,
              "Entity::kNullId must match entt::null");

namespace {
[[nodiscard]] entt::entity toEntt(Entity entity) noexcept { return static_cast<entt::entity>(entity.id()); }
} // namespace

Entity World::create() { return Entity{static_cast<Entity::Id>(m_registry.create())}; }

bool World::destroy(Entity entity) {
    if (!isAlive(entity)) {
        return false;
    }
    m_registry.destroy(toEntt(entity));
    return true;
}

bool World::isAlive(Entity entity) const noexcept {
    return !entity.isNull() && m_registry.valid(toEntt(entity));
}

std::size_t World::entityCount() const noexcept {
    // Live entities in the entity storage (excludes released slots kept for reuse).
    const auto* storage = m_registry.storage<entt::entity>();
    return storage != nullptr ? storage->free_list() : 0;
}

void World::clear() { m_registry.clear(); }

} // namespace zodar
