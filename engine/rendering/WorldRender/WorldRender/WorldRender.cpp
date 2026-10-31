#include "WorldRender/WorldRender.h"

#include <algorithm>

namespace zodar {

namespace {

bool renderable(const MeshRenderer& r) { return r.visible && r.mesh != nullptr; }

} // namespace

void collectDraws(const World& world, std::vector<gfx::MeshDraw>& out) {
    world.each<MeshRenderer>([&](Entity entity, const MeshRenderer& r) {
        if (renderable(r)) out.push_back({r.mesh, r.submesh, r.material, world.worldMatrix(entity)});
    });
}

WorldRenderSync::Stats WorldRenderSync::sync(const World& world) {
    Stats stats;
    const uint64_t generation = ++m_generation;
    world.each<MeshRenderer>([&](Entity entity, const MeshRenderer& r) {
        if (!renderable(r)) return;
        const glm::mat4 transform = world.worldMatrix(entity);
        auto it = m_tracked.find(entity.id());
        if (it != m_tracked.end() && (it->second.mesh != r.mesh || it->second.submesh != r.submesh)) {
            m_sink.remove(it->second.instance); // GpuScene has no setMesh: re-create.
            m_tracked.erase(it);
            it = m_tracked.end();
            ++stats.removed;
        }
        if (it == m_tracked.end()) {
            const gfx::InstanceHandle instance = m_sink.add(*r.mesh, r.submesh, r.material, transform);
            m_tracked.emplace(entity.id(), Tracked{instance, r.mesh, r.submesh, r.material, transform, generation});
            ++stats.added;
            return;
        }
        Tracked& t = it->second;
        t.generation = generation;
        bool changed = false;
        if (t.transform != transform) {
            m_sink.setTransform(t.instance, transform);
            t.transform = transform;
            changed = true;
        }
        if (t.material != r.material) {
            m_sink.setMaterial(t.instance, r.material);
            t.material = r.material;
            changed = true;
        }
        stats.updated += changed ? 1u : 0u;
    });

    std::vector<Entity::Id> stale;
    for (const auto& [id, t] : m_tracked) {
        if (t.generation != generation) stale.push_back(id);
    }
    std::sort(stale.begin(), stale.end());
    for (const Entity::Id id : stale) {
        m_sink.remove(m_tracked.at(id).instance);
        m_tracked.erase(id);
        ++stats.removed;
    }
    return stats;
}

void WorldRenderSync::clear() {
    std::vector<Entity::Id> ids;
    ids.reserve(m_tracked.size());
    for (const auto& entry : m_tracked) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    for (const Entity::Id id : ids) m_sink.remove(m_tracked.at(id).instance);
    m_tracked.clear();
}

gfx::InstanceHandle WorldRenderSync::instanceOf(Entity entity) const noexcept {
    const auto it = m_tracked.find(entity.id());
    return it == m_tracked.end() ? gfx::kInvalidInstance : it->second.instance;
}

} // namespace zodar
