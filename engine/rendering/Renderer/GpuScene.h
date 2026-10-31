#pragma once

#include "Assets/Bounds.h"
#include "Renderer/Material.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/GpuBuffer.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace zodar::gfx {

class ComputePipeline;
class Frustum;
class GeometryPool;
class GpuMesh;
class GraphicsPipeline;
class VulkanContext;

using InstanceHandle = uint32_t;
constexpr InstanceHandle kInvalidInstance = UINT32_MAX;

// Persistent, GPU-resident scene for GPU-driven rendering.
//
// Instances are registered once and stay in device-local buffers; only changed instances
// are uploaded. Each frame three compute passes cull every instance against the view
// frustum and compact the visible ones with an exclusive prefix sum (cull + workgroup scan,
// block-offset scan, scatter). The visible list keeps the group-sorted order, so draw order
// is deterministic and identical to the CPU path: (pipeline, material, mesh, submesh,
// insertion order). Multi-draw indirect commands (one per group) get their instance range
// from the same prefix sum. The CPU cost per frame is independent of the instance count,
// except for blended instances, which are still sorted back-to-front on the CPU.
//
// Requirements: meshes must come from one GeometryPool. Instance data uses the same
// transforms, normal matrices and world bounds as the CPU path, so both paths render
// identical images.
class GpuScene {
public:
    GpuScene(VulkanContext& context, MaterialSystem& materials, const std::filesystem::path& shaderDirectory,
             uint32_t framesInFlight);
    ~GpuScene();

    GpuScene(const GpuScene&) = delete;
    GpuScene& operator=(const GpuScene&) = delete;

    InstanceHandle add(const GpuMesh& mesh, uint32_t submesh, MaterialHandle material, const glm::mat4& transform);
    void setTransform(InstanceHandle instance, const glm::mat4& transform);
    void setMaterial(InstanceHandle instance, MaterialHandle material);
    void remove(InstanceHandle instance);

    uint32_t instanceCount() const { return m_alive; }
    uint32_t groupCount() const { return static_cast<uint32_t>(m_groups.size()); }
    VkDeviceSize gpuBytes() const;

    // Results of the GPU cull for the frame that last used `slot` (framesInFlight old).
    struct CullStats {
        uint32_t visible = 0;
        uint32_t drawCommands = 0; // Indirect commands with at least one instance.
        uint32_t triangles = 0;
        bool valid = false;
    };

    // --- Used by SceneRenderer ---------------------------------------------------------
    struct FramePasses {
        RGBuffer instances;
        RGBuffer visible;
        RGBuffer commands;
        RGBuffer statsBuffer;
        VkDeviceAddress instanceAddress = 0;
        VkDeviceAddress visibleAddress = 0;
        CullStats stats;
    };
    // Adds the update + cull passes for this frame. Call before the pass that draws.
    FramePasses addCullPasses(RenderGraph& graph, const FrameInfo& frame, const Frustum& frustum);
    // Adds the stats readback pass; call after the pass that draws.
    void addStatsPass(RenderGraph& graph, const FrameInfo& frame, const FramePasses& passes);
    // vkCmdDrawIndexedIndirect calls recordIndirectDraws() issues (one per pipeline).
    uint32_t indirectCallCount() const { return static_cast<uint32_t>(m_buckets.size()); }
    const GeometryPool* geometryPool() const { return m_pool; }
    // Records the indirect draws (pipeline layout and frame push constants already bound).
    // Returns the number of draw commands recorded (vkCmdDraw* calls).
    // depthOnly: depth-prepass pipelines (MaterialSystem::depthPipeline) instead.
    uint32_t recordIndirectDraws(VkCommandBuffer cmd, const FramePasses& passes, bool depthOnly = false) const;

    // Testing/diagnostics: the visible list of the last executed frame (instance handles in
    // draw order). Synchronous readback; do not use in a frame loop.
    std::vector<InstanceHandle> readVisibleList(class UploadContext& upload) const;

    struct BlendedInstance {
        uint32_t slot;
        const GpuMesh* mesh;
        uint32_t submesh;
        MaterialHandle material;
        Bounds world;
    };
    const std::vector<uint32_t>& blendedSlots() const { return m_blended; }
    BlendedInstance blended(uint32_t slot) const;

private:
    struct Instance {
        const GpuMesh* mesh = nullptr;
        uint32_t submesh = 0;
        MaterialHandle material = 0;
        glm::mat4 transform{1.0f};
        Bounds world;
        uint64_t sequence = 0; // Insertion order (CPU-path submission order).
        bool alive = false;
        bool blend = false;
    };
    struct Group {
        const GraphicsPipeline* pipeline;
        MaterialHandle material;
        const GpuMesh* mesh;
        uint32_t submesh;
        uint32_t count;
        uint32_t base;
    };
    struct Bucket {
        const GraphicsPipeline* pipeline;
        uint32_t firstGroup;
        uint32_t groupCount;
    };

    void markDirty(uint32_t slot);
    void rebuildGroups();
    void ensureCapacity();

    VulkanContext& m_context;
    MaterialSystem& m_materials;
    const GeometryPool* m_pool = nullptr;
    std::unique_ptr<ComputePipeline> m_cullScan;
    std::unique_ptr<ComputePipeline> m_cullBlocks;
    std::unique_ptr<ComputePipeline> m_cullScatter;

    std::vector<Instance> m_instances;
    std::vector<uint32_t> m_free;
    std::vector<uint32_t> m_blended;
    std::vector<uint32_t> m_dirty;
    std::vector<bool> m_isDirty;
    uint32_t m_alive = 0;
    uint64_t m_nextSequence = 0;
    bool m_structureDirty = false;

    std::vector<Group> m_groups;
    std::vector<Bucket> m_buckets;
    std::vector<uint32_t> m_order; // Slot per position, group-sorted (GPU-drawn instances only).
    bool m_orderDirty = false;      // Order, groups and static command fields need an upload.

    uint32_t m_capacity = 0;
    GpuBuffer m_instanceBuffer; // GpuInstance per slot
    GpuBuffer m_cullBuffer;     // CullData per slot
    GpuBuffer m_orderBuffer;    // Slot per position
    GpuBuffer m_prefixBuffer;   // Per position: (workgroup-local exclusive prefix << 1) | visible
    GpuBuffer m_blockSums;      // Visible count per 256-position block
    GpuBuffer m_blockOffsets;   // Exclusive prefix of m_blockSums, plus the total
    GpuBuffer m_visibleBuffer;  // Slot per visible instance, in draw order
    GpuBuffer m_groupBuffer;    // (base position, count) per group
    GpuBuffer m_commandBuffer;  // Draw commands; firstInstance/instanceCount written by the cull
    GpuBuffer m_statsBuffer;
    std::vector<GpuBuffer> m_readback; // Per frame slot
    std::vector<bool> m_readbackWritten;
};

} // namespace zodar::gfx
