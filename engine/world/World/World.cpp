#include "World/World.h"

#include <algorithm>
#include <type_traits>

#include <glm/gtc/matrix_transform.hpp>

namespace zodar {

static_assert(std::is_same_v<std::underlying_type_t<entt::entity>, Entity::Id>,
              "Entity::Id must match the EnTT entity type");
static_assert(static_cast<Entity::Id>(entt::entity{entt::null}) == Entity::kNullId,
              "Entity::kNullId must match entt::null");

glm::mat4 Transform::localMatrix() const noexcept {
    glm::mat4 m = glm::translate(glm::mat4{1.0f}, position);
    m *= glm::mat4_cast(rotation);
    return glm::scale(m, scale);
}

Entity World::create() { return Entity{static_cast<Entity::Id>(m_registry.create())}; }

bool World::destroy(Entity entity) {
    if (!isAlive(entity)) {
        return false;
    }
    detachFromParent(entity);
    // Collect the subtree first (iterative, no recursion depth limit), then destroy it.
    std::vector<Entity> subtree{entity};
    for (std::size_t i = 0; i < subtree.size(); ++i) {
        if (const auto* node = m_registry.try_get<detail::HierarchyNode>(toEntt(subtree[i]))) {
            subtree.insert(subtree.end(), node->children.begin(), node->children.end());
        }
    }
    for (const Entity e : subtree) {
        m_registry.destroy(toEntt(e));
    }
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

void World::detachFromParent(Entity entity) {
    auto* node = m_registry.try_get<detail::HierarchyNode>(toEntt(entity));
    if (node == nullptr || node->parent.isNull()) {
        return;
    }
    auto& siblings = m_registry.get<detail::HierarchyNode>(toEntt(node->parent)).children;
    siblings.erase(std::find(siblings.begin(), siblings.end(), entity));
    node->parent = Entity::null();
}

bool World::setParent(Entity child, Entity parent) {
    if (!isAlive(child)) {
        return false;
    }
    if (!parent.isNull() && (!isAlive(parent) || parent == child || isDescendantOf(parent, child))) {
        return false;
    }
    if (this->parent(child) == parent) {
        return true;
    }
    detachFromParent(child);
    if (parent.isNull()) {
        return true;
    }
    m_registry.get_or_emplace<detail::HierarchyNode>(toEntt(parent)).children.push_back(child);
    m_registry.get_or_emplace<detail::HierarchyNode>(toEntt(child)).parent = parent;
    return true;
}

Entity World::parent(Entity entity) const noexcept {
    const auto* node = tryGet<detail::HierarchyNode>(entity);
    return node != nullptr ? node->parent : Entity::null();
}

std::span<const Entity> World::children(Entity entity) const noexcept {
    const auto* node = tryGet<detail::HierarchyNode>(entity);
    return node != nullptr ? std::span<const Entity>{node->children} : std::span<const Entity>{};
}

bool World::isDescendantOf(Entity entity, Entity ancestor) const noexcept {
    if (ancestor.isNull()) {
        return false;
    }
    for (Entity p = parent(entity); !p.isNull(); p = parent(p)) {
        if (p == ancestor) {
            return true;
        }
    }
    return false;
}

glm::mat4 World::localMatrix(Entity entity) const noexcept {
    const auto* transform = tryGet<Transform>(entity);
    return transform != nullptr ? transform->localMatrix() : glm::mat4{1.0f};
}

glm::mat4 World::worldMatrix(Entity entity) const noexcept {
    glm::mat4 m = localMatrix(entity);
    for (Entity p = parent(entity); !p.isNull(); p = parent(p)) {
        m = localMatrix(p) * m;
    }
    return m;
}

} // namespace zodar
