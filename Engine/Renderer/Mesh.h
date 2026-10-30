#pragma once

#include "Assets/MeshData.h"
#include "Renderer/Resources/GpuBuffer.h"

#include <string>
#include <vector>

namespace zodar::gfx {

class UploadBatch;

// Mesh resident in device-local memory. Vertex layout: zodar::Vertex.
class GpuMesh {
public:
    GpuMesh() = default;
    // Validates `data` and records its upload into `batch` (usable after batch.submit()).
    GpuMesh(UploadBatch& batch, const MeshData& data);

    GpuMesh(GpuMesh&&) noexcept = default;
    GpuMesh& operator=(GpuMesh&&) noexcept = default;

    const GpuBuffer& vertexBuffer() const { return m_vertices; }
    const GpuBuffer& indexBuffer() const { return m_indices; }
    const std::vector<Submesh>& submeshes() const { return m_submeshes; }
    const Bounds& bounds() const { return m_bounds; }
    uint32_t vertexCount() const { return m_vertexCount; }
    uint32_t indexCount() const { return m_indexCount; }
    const std::string& name() const { return m_name; }

private:
    GpuBuffer m_vertices;
    GpuBuffer m_indices;
    std::vector<Submesh> m_submeshes;
    Bounds m_bounds;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
    std::string m_name;
};

} // namespace zodar::gfx
