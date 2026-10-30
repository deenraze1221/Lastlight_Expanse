#include "Renderer/Mesh.h"

#include "Core/Error.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/Resources/UploadContext.h"

namespace zodar::gfx {

GpuMesh::GpuMesh(UploadBatch& batch, const MeshData& data, GeometryPool* pool)
    : m_pool(pool), m_submeshes(data.submeshes), m_bounds(data.bounds), m_vertexCount(static_cast<uint32_t>(data.vertices.size())),
      m_indexCount(static_cast<uint32_t>(data.indices.size())), m_name(data.name) {
    const std::string error = data.validate();
    if (!error.empty()) {
        fail("Mesh '" + data.name + "' is invalid: " + error);
    }
    ZODAR_ENSURE(!data.vertices.empty() && !data.indices.empty(), "mesh '" + data.name + "' is empty");
    if (m_submeshes.empty()) {
        m_submeshes.push_back({0, m_indexCount, 0, data.bounds});
    }
    if (m_pool) {
        const GeometryPool::Allocation a = m_pool->upload(batch, data.vertices, data.indices);
        m_baseVertex = a.baseVertex;
        m_baseIndex = a.baseIndex;
        return;
    }
    const std::string vbName = data.name + ".vertices";
    const std::string ibName = data.name + ".indices";
    m_vertices = batch.createBuffer(data.vertices.data(), data.vertices.size() * sizeof(Vertex),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vbName.c_str());
    m_indices = batch.createBuffer(data.indices.data(), data.indices.size() * sizeof(uint32_t),
                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ibName.c_str());
}

VkBuffer GpuMesh::vertexBuffer() const {
    return m_pool ? m_pool->vertexBuffer() : m_vertices.handle();
}

VkBuffer GpuMesh::indexBuffer() const {
    return m_pool ? m_pool->indexBuffer() : m_indices.handle();
}

} // namespace zodar::gfx
