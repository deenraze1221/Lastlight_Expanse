#include "Assets/MeshData.h"

#include <cmath>

namespace raven {

void MeshData::computeBounds() {
    bounds = {};
    for (auto& submesh : submeshes) {
        submesh.bounds = {};
        for (uint32_t i = 0; i < submesh.indexCount; ++i) {
            submesh.bounds.expand(vertices[indices[submesh.firstIndex + i]].position);
        }
        bounds.expand(submesh.bounds);
    }
}

void MeshData::generateNormals() {
    for (auto& v : vertices) {
        v.normal = glm::vec3(0.0f);
    }
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Vertex& a = vertices[indices[i]];
        Vertex& b = vertices[indices[i + 1]];
        Vertex& c = vertices[indices[i + 2]];
        // Cross product length is twice the triangle area: larger faces weigh more.
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (auto& v : vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-20f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void MeshData::generateTangents() {
    std::vector<glm::vec3> tan(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitan(vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];
        const glm::vec3 e1 = v1.position - v0.position;
        const glm::vec3 e2 = v2.position - v0.position;
        const glm::vec2 d1 = v1.uv0 - v0.uv0;
        const glm::vec2 d2 = v2.uv0 - v0.uv0;
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-12f) {
            continue; // Degenerate UVs contribute nothing.
        }
        const float r = 1.0f / det;
        const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;
        for (uint32_t idx : {i0, i1, i2}) {
            tan[idx] += t;
            bitan[idx] += b;
        }
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3 n = vertices[i].normal;
        glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);
        if (glm::dot(t, t) < 1e-20f) {
            // No usable UV gradient: any vector perpendicular to the normal.
            t = std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0)) : glm::cross(n, glm::vec3(0, 1, 0));
        }
        t = glm::normalize(t);
        const float sign = glm::dot(glm::cross(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = glm::vec4(t, sign);
    }
}

std::string MeshData::validate() const {
    if (indices.size() % 3 != 0) {
        return "index count is not a multiple of 3";
    }
    for (uint32_t index : indices) {
        if (index >= vertices.size()) {
            return "index out of range";
        }
    }
    for (const auto& s : submeshes) {
        if (s.indexCount % 3 != 0 || static_cast<size_t>(s.firstIndex) + s.indexCount > indices.size()) {
            return "submesh range invalid";
        }
    }
    return {};
}

} // namespace raven
