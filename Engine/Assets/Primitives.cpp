#include "Assets/Primitives.h"

#include <glm/gtc/constants.hpp>

namespace raven::primitives {
namespace {

void finish(MeshData& mesh) {
    mesh.submeshes = {{0, static_cast<uint32_t>(mesh.indices.size()), 0, {}}};
    mesh.generateTangents();
    mesh.computeBounds();
}

} // namespace

MeshData cube(float size) {
    MeshData mesh;
    mesh.name = "Cube";
    const float h = size * 0.5f;
    // Each face: normal, then the "u" and "v" axes so that (u x v) == normal (CCW front faces).
    const glm::vec3 faces[6][3] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    for (const auto& f : faces) {
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const auto& c : corners) {
            Vertex v;
            v.position = (f[0] + f[1] * c.x + f[2] * c.y) * h;
            v.normal = f[0];
            v.uv0 = {(c.x + 1) * 0.5f, 1.0f - (c.y + 1) * 0.5f};
            mesh.vertices.push_back(v);
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    finish(mesh);
    return mesh;
}

MeshData plane(float width, float depth, uint32_t subdivisions, float uvScale) {
    MeshData mesh;
    mesh.name = "Plane";
    const uint32_t n = subdivisions < 1 ? 1 : subdivisions;
    for (uint32_t z = 0; z <= n; ++z) {
        for (uint32_t x = 0; x <= n; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(n);
            const float v = static_cast<float>(z) / static_cast<float>(n);
            Vertex vert;
            vert.position = {(u - 0.5f) * width, 0.0f, (v - 0.5f) * depth};
            vert.normal = {0, 1, 0};
            vert.uv0 = glm::vec2(u, v) * uvScale;
            mesh.vertices.push_back(vert);
        }
    }
    for (uint32_t z = 0; z < n; ++z) {
        for (uint32_t x = 0; x < n; ++x) {
            const uint32_t i = z * (n + 1) + x;
            // Counter-clockwise when seen from +Y.
            mesh.indices.insert(mesh.indices.end(), {i, i + n + 1, i + 1, i + 1, i + n + 1, i + n + 2});
        }
    }
    finish(mesh);
    return mesh;
}

MeshData uvSphere(float radius, uint32_t segments, uint32_t rings) {
    MeshData mesh;
    mesh.name = "Sphere";
    segments = segments < 3 ? 3 : segments;
    rings = rings < 2 ? 2 : rings;
    for (uint32_t r = 0; r <= rings; ++r) {
        const float v = static_cast<float>(r) / static_cast<float>(rings);
        const float phi = v * glm::pi<float>();
        for (uint32_t s = 0; s <= segments; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(segments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), -std::sin(phi) * std::sin(theta)};
            Vertex vert;
            vert.position = n * radius;
            vert.normal = n;
            vert.uv0 = {u, v};
            mesh.vertices.push_back(vert);
        }
    }
    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < segments; ++s) {
            const uint32_t a = r * (segments + 1) + s;
            const uint32_t b = a + segments + 1;
            // Rows touching a pole collapse to one point: skip their zero-area triangle.
            if (r != 0) {
                mesh.indices.insert(mesh.indices.end(), {a, b, a + 1});
            }
            if (r != rings - 1) {
                mesh.indices.insert(mesh.indices.end(), {a + 1, b, b + 1});
            }
        }
    }
    finish(mesh);
    return mesh;
}

} // namespace raven::primitives
