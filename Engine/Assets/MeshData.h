#pragma once

#include "Assets/Bounds.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace raven {

// The engine's single static vertex format (64 bytes). Colours are linear RGBA.
// Tangent.w stores the bitangent sign (glTF convention).
struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 uv0{0.0f};
    glm::vec4 color{1.0f};
};
static_assert(sizeof(Vertex) == 64, "Vertex layout is shared with shaders");

// A range of the index buffer drawn with one material.
struct Submesh {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0; // Index into the owning model's materials (or caller-defined).
    Bounds bounds;
};

// CPU-side triangle mesh (triangle list, 32-bit indices).
struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;
    Bounds bounds;

    // Recomputes mesh and submesh bounds from referenced vertices.
    void computeBounds();
    // Area-weighted smooth normals from triangles (replaces existing normals).
    void generateNormals();
    // Per-vertex tangents from UVs (Lengyel's method, Gram-Schmidt orthogonalised).
    // Close to, but not bit-identical with, MikkTSpace.
    void generateTangents();
    // Checks index range, triangle-list shape and submesh ranges. Returns an error
    // message, or an empty string when valid.
    std::string validate() const;
};

} // namespace raven
