#pragma once

#include <glm/glm.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace zodar::voxel {

// Voxel = material id. 0 is empty space; every other id is solid and opaque and indexes a
// VoxelPalette entry. Meaning of ids is defined by the game.
using Voxel = uint16_t;
constexpr Voxel kEmpty = 0;

constexpr int kChunkShift = 5;
constexpr int kChunkSize = 1 << kChunkShift; // voxels per chunk edge
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;
// Coarsest LOD: one cell spans the whole chunk edge (32 >> 5 = 1).
constexpr int kMaxLod = kChunkShift;

// Integer chunk position. Chunk (x, y, z) covers voxels [x*32, x*32+32) etc.
struct ChunkCoord {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    auto operator<=>(const ChunkCoord&) const = default;

    ChunkCoord offset(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
    glm::ivec3 originVoxel() const { return {x * kChunkSize, y * kChunkSize, z * kChunkSize}; }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const noexcept {
        uint64_t h = static_cast<uint32_t>(c.x);
        h = h * 0x9E3779B97F4A7C15ull ^ static_cast<uint32_t>(c.y);
        h = h * 0x9E3779B97F4A7C15ull ^ static_cast<uint32_t>(c.z);
        return static_cast<size_t>(h ^ (h >> 29));
    }
};

// Chunk containing a voxel (floor division; C++20 guarantees arithmetic right shift).
inline ChunkCoord chunkOf(const glm::ivec3& voxel) {
    return {voxel.x >> kChunkShift, voxel.y >> kChunkShift, voxel.z >> kChunkShift};
}
inline glm::ivec3 localOf(const glm::ivec3& voxel) {
    return voxel & glm::ivec3(kChunkSize - 1);
}

// Dense 32^3 voxel block with a uniform-fill fast path: chunks that are entirely one value
// (all air above the terrain, all rock below it) store no array at all.
class VoxelChunk {
public:
    explicit VoxelChunk(Voxel fill = kEmpty) : m_fill(fill) {}

    static int index(int x, int y, int z) { return x | (z << kChunkShift) | (y << (2 * kChunkShift)); }

    Voxel get(int x, int y, int z) const { return m_data.empty() ? m_fill : m_data[index(x, y, z)]; }
    void set(int x, int y, int z, Voxel v);
    // Fills a whole Y layer (fast path for generators).
    void fillLayer(int y, Voxel v);

    bool isUniform() const { return m_data.empty(); }
    // Valid only when isUniform().
    Voxel uniformValue() const { return m_fill; }
    // True if the chunk contains no solid voxel / no empty voxel.
    bool isEmpty() const;
    bool isFull() const;
    // Drops the array if every voxel has the same value.
    void compact();
    size_t memoryBytes() const { return m_data.size() * sizeof(Voxel); }

private:
    std::vector<Voxel> m_data;
    Voxel m_fill;
};

} // namespace zodar::voxel
