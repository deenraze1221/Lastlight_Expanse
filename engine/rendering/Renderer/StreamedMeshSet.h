#pragma once

#include "Assets/MeshData.h"
#include "Renderer/Material.h"
#include "Renderer/Resources/UploadQueue.h"
#include "Renderer/SceneRenderer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace zodar::gfx {

class GpuMesh;
class UploadBatch;
class UploadContext;
class VulkanContext;

// GPU residency for meshes that come and go at runtime (streamed terrain chunks, procedural
// props). Keyed by a caller-defined 64-bit id.
//
// - upsert() records the upload into a shared batch; update() submits it asynchronously on
//   the transfer queue. A replaced mesh keeps being drawn until its successor is ready, so
//   LOD switches and edits never leave holes.
// - Replaced and removed meshes are released through the context's deletion queue, after
//   every frame that may still draw them has completed.
// - Meshes own their buffers (not the shared GeometryPool, which cannot free), and are drawn
//   through the CPU draw path (SceneView::draws) with frustum culling.
//
// Requires Renderer::attachUploads(&upload.queue()) so finished uploads are acquired at
// frame start. Main thread only.
class StreamedMeshSet {
public:
    StreamedMeshSet(VulkanContext& context, UploadContext& upload);
    ~StreamedMeshSet();

    StreamedMeshSet(const StreamedMeshSet&) = delete;
    StreamedMeshSet& operator=(const StreamedMeshSet&) = delete;

    // False leaves the old mesh intact; caller retains/retries its CPU event.
    bool tryUpsert(uint64_t key, const MeshData& mesh, const glm::mat4& transform, MaterialHandle material,
                   uint64_t budgetBytes);
    void upsert(uint64_t key, const MeshData& mesh, const glm::mat4& transform, MaterialHandle material);
    void remove(uint64_t key);
    // Submits recorded uploads and promotes meshes whose upload is ready. Call once per frame
    // before appendDraws().
    void update();
    void appendDraws(std::vector<MeshDraw>& out) const;

    struct Stats {
        uint32_t meshes = 0;         // drawable
        uint32_t pendingUploads = 0; // meshes waiting for their upload
        uint64_t triangles = 0;      // drawable triangles
        uint64_t gpuBytes = 0;       // vertex + index bytes of drawable and pending meshes
        uint64_t deferredBytes = 0; // buffers retained until the deletion timeline completes
        uint64_t stagingBytes = 0; // conservative payload estimate for pending uploads
        uint64_t uploadedBytes = 0;  // total since creation
    };
    Stats stats() const;

private:
    struct Slot {
        std::shared_ptr<GpuMesh> live;
        glm::mat4 liveTransform{1.0f};
        MaterialHandle liveMaterial = 0;
        std::shared_ptr<GpuMesh> pending;
        glm::mat4 pendingTransform{1.0f};
        MaterialHandle pendingMaterial = 0;
        UploadTicket ticket;     // 0 while the pending upload is still in the open batch
    };

    void retire(std::shared_ptr<GpuMesh> mesh);
    void recountStats();

    VulkanContext& m_context;
    UploadContext& m_upload;
    std::unique_ptr<UploadBatch> m_batch;
    std::vector<uint64_t> m_inBatch; // keys whose pending mesh is in m_batch
    std::unordered_map<uint64_t, Slot> m_slots;
    Stats m_stats;
    std::shared_ptr<uint64_t> m_deferredBytes = std::make_shared<uint64_t>(0);
};

} // namespace zodar::gfx
