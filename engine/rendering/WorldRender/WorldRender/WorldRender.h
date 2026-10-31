#pragma once

#include "Renderer/GpuScene.h"
#include "Renderer/Material.h"
#include "Renderer/SceneRenderer.h"
#include "World/World.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// World-to-renderer bridge (Phase B1.3). World stays CPU-only; this layer reads entities with
// a MeshRenderer component and their world transforms (World::worldMatrix) and feeds either
// the CPU draw path (SceneView::draws) or a persistent GpuScene.
namespace zodar {

// Renders one submesh of a GPU mesh with a material at the entity's world transform.
// Entities with a null mesh or visible == false are not rendered. The mesh must outlive
// every frame that draws it (and, with WorldRenderSync, the synced instance).
struct MeshRenderer {
    const gfx::GpuMesh* mesh = nullptr;
    uint32_t submesh = 0;
    gfx::MaterialHandle material = 0;
    bool visible = true;
};

// CPU path: appends one MeshDraw per renderable entity to `out` (in World::each order).
// Stateless; call once per frame and pass the result as SceneView::draws.
void collectDraws(const World& world, std::vector<gfx::MeshDraw>& out);

// Destination of WorldRenderSync. GpuSceneSink adapts gfx::GpuScene; tests use fakes.
class RenderInstanceSink {
public:
    virtual ~RenderInstanceSink() = default;
    virtual gfx::InstanceHandle add(const gfx::GpuMesh& mesh, uint32_t submesh, gfx::MaterialHandle material,
                                    const glm::mat4& transform) = 0;
    virtual void setTransform(gfx::InstanceHandle instance, const glm::mat4& transform) = 0;
    virtual void setMaterial(gfx::InstanceHandle instance, gfx::MaterialHandle material) = 0;
    virtual void remove(gfx::InstanceHandle instance) = 0;
};

class GpuSceneSink final : public RenderInstanceSink {
public:
    explicit GpuSceneSink(gfx::GpuScene& scene) : m_scene(scene) {}
    gfx::InstanceHandle add(const gfx::GpuMesh& mesh, uint32_t submesh, gfx::MaterialHandle material,
                            const glm::mat4& transform) override {
        return m_scene.add(mesh, submesh, material, transform);
    }
    void setTransform(gfx::InstanceHandle instance, const glm::mat4& transform) override {
        m_scene.setTransform(instance, transform);
    }
    void setMaterial(gfx::InstanceHandle instance, gfx::MaterialHandle material) override {
        m_scene.setMaterial(instance, material);
    }
    void remove(gfx::InstanceHandle instance) override { m_scene.remove(instance); }

private:
    gfx::GpuScene& m_scene;
};

// GPU-driven path: keeps one sink instance per renderable entity. sync() diffs the World
// against the last sync: new renderables are added (World::each order), changed world
// matrices and materials are updated, mesh/submesh changes re-create the instance, and
// destroyed, hidden or MeshRenderer-less entities are removed (in entity-id order, so results
// are deterministic). World matrices are recomputed each sync (O(entities x depth)).
// The sink must outlive this object; the destructor does not touch it (call clear() first
// to remove the instances).
class WorldRenderSync {
public:
    explicit WorldRenderSync(RenderInstanceSink& sink) : m_sink(sink) {}
    WorldRenderSync(const WorldRenderSync&) = delete;
    WorldRenderSync& operator=(const WorldRenderSync&) = delete;

    struct Stats {
        uint32_t added = 0;
        uint32_t updated = 0; // Instances whose transform and/or material changed.
        uint32_t removed = 0; // Includes the removal half of a mesh/submesh re-create.
    };
    Stats sync(const World& world);

    // Removes every synced instance from the sink.
    void clear();

    [[nodiscard]] std::size_t instanceCount() const noexcept { return m_tracked.size(); }
    // Instance of a synced entity, or gfx::kInvalidInstance.
    [[nodiscard]] gfx::InstanceHandle instanceOf(Entity entity) const noexcept;

private:
    struct Tracked {
        gfx::InstanceHandle instance = gfx::kInvalidInstance;
        const gfx::GpuMesh* mesh = nullptr;
        uint32_t submesh = 0;
        gfx::MaterialHandle material = 0;
        glm::mat4 transform{1.0f};
        uint64_t generation = 0;
    };

    RenderInstanceSink& m_sink;
    std::unordered_map<Entity::Id, Tracked> m_tracked;
    uint64_t m_generation = 0;
};

} // namespace zodar
