#pragma once

#include "Assets/MeshData.h"
#include "Scene/Voxel/BlockRegistry.h"
#include "Scene/Voxel/VoxelChunk.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace zodar::voxel {

// A chunk and its 26 neighbours. Missing neighbours (nullptr) read as empty space.
struct ChunkNeighborhood {
    // Index: (dx + 1) + (dy + 1) * 3 + (dz + 1) * 9; the centre chunk is 13.
    std::array<const VoxelChunk*, 27> chunks{};

    static constexpr int slot(int dx, int dy, int dz) { return (dx + 1) + (dy + 1) * 3 + (dz + 1) * 9; }
    const VoxelChunk* center() const { return chunks[13]; }

    // Voxel at centre-chunk-local coordinates in [-32, 64) on each axis.
    Voxel get(int x, int y, int z) const {
        const VoxelChunk* c = chunks[slot((x >> kChunkShift), (y >> kChunkShift), (z >> kChunkShift))];
        return c ? c->get(x & (kChunkSize - 1), y & (kChunkSize - 1), z & (kChunkSize - 1)) : kEmpty;
    }
};

struct MeshOptions {
    // Level of detail: cells are 2^lod voxels wide (lod 0 = full resolution, kMaxLod = one
    // cell per chunk). A cell is solid when at least half of its voxels are; it takes the
    // material of its topmost solid voxel (the surface seen from above).
    int lod = 0;
    float voxelSize = 1.0f; // world units per voxel
    // Bit (1 << Face): treat the neighbour on that side as empty, which emits the chunk's
    // boundary faces there. Used between chunks of different LOD so the mismatching
    // surfaces are joined by walls instead of leaving cracks.
    uint8_t sealFaces = 0;
    bool ambientOcclusion = true;
    // Vertex colour multiplier for 0..3 occluding neighbours (3 = unoccluded).
    std::array<float, 4> aoCurve{0.45f, 0.64f, 0.82f, 1.0f};
};

struct MeshStats {
    uint32_t quads = 0;
    uint32_t faces = 0; // unmerged voxel faces the quads represent
};

// Greedy mesher with per-vertex ambient occlusion. Coplanar faces with the same material
// and uniform occlusion are merged into one quad; faces with varying occlusion are emitted
// individually so the interpolated AO stays exact. Triangles are flipped along the AO
// gradient to avoid the usual anisotropy. Output is in chunk-local world units (the chunk
// origin is the caller's transform), one submesh with material index 0, CCW front faces.
// The result is deterministic for identical inputs.
MeshData meshChunk(const ChunkNeighborhood& neighborhood, const VoxelPalette& palette, const MeshOptions& options,
                   MeshStats* stats = nullptr);

} // namespace zodar::voxel
