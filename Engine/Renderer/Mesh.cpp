#include "Renderer/Mesh.h"

#include "Core/Error.h"
#include "Renderer/Resources/UploadContext.h"

namespace zodar::gfx {

GpuMesh::GpuMesh(UploadBatch& batch, const MeshData& data)
    : m_submeshes(data.submeshes), m_bounds(data.bounds), m_vertexCount(static_cast<uint32_t>(data.vertices.size())),
      m_indexCount(static_cast<uint32_t>(data.indices.size())), m_name(data.name) {
    const std::string error = data.validate();
    if (!error.empty()) {
        fail("Mesh '" + data.name + "' is invalid: " + error);
    }
    ZODAR_ENSURE(!data.vertices.empty() && !data.indices.empty(), "mesh '" + data.name + "' is empty");
    if (m_submeshes.empty()) {
        m_submeshes.push_back({0, m_indexCount, 0, data.bounds});
    }
    const std::string vbName = data.name + ".vertices";
    const std::string ibName = data.name + ".indices";
    m_vertices = batch.createBuffer(data.vertices.data(), data.vertices.size() * sizeof(Vertex),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vbName.c_str());
    m_indices = batch.createBuffer(data.indices.data(), data.indices.size() * sizeof(uint32_t),
                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ibName.c_str());
}

} // namespace zodar::gfx
