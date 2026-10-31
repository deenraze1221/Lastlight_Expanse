#pragma once

#include "Assets/MeshData.h"
#include "Renderer/Resources/GpuBuffer.h"

#include <string>
#include <vector>

namespace zodar::gfx {

class GeometryPool;
class UploadBatch;

// Mesh resident in device-local memory. Vertex layout: zodar::Vertex.
// With a GeometryPool the mesh lives in the pool's shared buffers (required for
// GPU-driven drawing); otherwise it owns its own vertex and index buffers.
class GpuMesh {
public:
    GpuMesh() = default;
    // Validates `data` and records its upload into `batch` (usable after batch.submit()).
    GpuMesh(UploadBatch& batch, const MeshData& data, GeometryPool* pool = nullptr);

    GpuMesh(GpuMesh&&) noexcept = default;
    GpuMesh& operator=(GpuMesh&&) noexcept = default;

    // Buffers to bind for drawing. Draws use firstIndex = baseIndex() + submesh.firstIndex
    // and vertexOffset = baseVertex() (both 0 for meshes with their own buffers).
    VkBuffer vertexBuffer() const;
    VkBuffer indexBuffer() const;
    uint32_t baseVertex() const { return m_baseVertex; }
    uint32_t baseIndex() const { return m_baseIndex; }
    const GeometryPool* pool() const { return m_pool; }
    const std::vector<Submesh>& submeshes() const { return m_submeshes; }
    const Bounds& bounds() const { return m_bounds; }
    uint32_t vertexCount() const { return m_vertexCount; }
    uint32_t indexCount() const { return m_indexCount; }
    const std::string& name() const { return m_name; }

private:
    GeometryPool* m_pool = nullptr;
    GpuBuffer m_vertices;
    GpuBuffer m_indices;
    uint32_t m_baseVertex = 0;
    uint32_t m_baseIndex = 0;
    std::vector<Submesh> m_submeshes;
    Bounds m_bounds;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    std::string m_name;
};

} // namespace zodar::gfx
