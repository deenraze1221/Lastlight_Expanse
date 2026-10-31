#pragma once

#include "Assets/MeshData.h"

#include <cstdint>

namespace zodar::primitives {

// Procedural meshes with normals, tangents and UVs; one submesh with material 0.
// Useful for tests, prototyping and debug drawing.
MeshData cube(float size = 1.0f);
MeshData plane(float width = 1.0f, float depth = 1.0f, uint32_t subdivisions = 1, float uvScale = 1.0f);
MeshData uvSphere(float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);

} // namespace zodar::primitives
