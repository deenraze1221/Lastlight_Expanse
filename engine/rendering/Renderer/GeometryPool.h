#pragma once

#include "Assets/MeshData.h"
#include "Renderer/Resources/GpuBuffer.h"

#include <cstdint>
#include <span>

namespace zodar::gfx {

class UploadBatch;
class VulkanContext;

// Shared device-local vertex and index buffers. Meshes created in a pool share one
// vertex/index buffer pair, so many meshes can be drawn by a single multi-draw indirect
// call (indices stay mesh-relative; draws use the mesh's base vertex as vertexOffset).
// Allocation is linear; the buffers grow (doubling) on demand. Freeing individual meshes
// is not supported yet (meshes live as long as the pool).
class GeometryPool {
public:
    struct Allocation {
        uint32_t baseVertex = 0;
        uint32_t baseIndex = 0;
    };

    GeometryPool(VulkanContext& context, uint32_t initialVertices = 1u << 18, uint32_t initialIndices = 1u << 20);

    GeometryPool(const GeometryPool&) = delete;
    GeometryPool& operator=(const GeometryPool&) = delete;

    // Records the upload into `batch`; the data is usable after batch.submit().
    Allocation upload(UploadBatch& batch, std::span<const Vertex> vertices, std::span<const uint32_t> indices);

    VkBuffer vertexBuffer() const { return m_vertices.handle(); }
    VkBuffer indexBuffer() const { return m_indices.handle(); }
    uint32_t vertexCount() const { return m_vertexCount; }
    uint32_t indexCount() const { return m_indexCount; }
    uint32_t vertexCapacity() const { return m_vertexCapacity; }
    uint32_t indexCapacity() const { return m_indexCapacity; }
    VkDeviceSize bytesReserved() const { return m_vertices.size() + m_indices.size(); }

private:
    void grow(UploadBatch& batch, uint32_t minVertices, uint32_t minIndices);

    VulkanContext& m_context;
    GpuBuffer m_vertices;
    GpuBuffer m_indices;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    uint32_t m_vertexCapacity = 0;
    uint32_t m_indexCapacity = 0;
};

} // namespace zodar::gfx
