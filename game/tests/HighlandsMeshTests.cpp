// Highlands regression (Phase V1): golden hashes of the cube-only mesher output for chunks of
// the Highlands generator (seed 1337, the game's default) with the game palette and face
// layers, captured on the pre-V1 mesher. The Highlands scene must stay byte-identical
// (docs/guide/VOXELS.md §12). Generator values come from float noise: goldens are for x86-64
// builds without FMA contraction (GCC/Clang/MinGW defaults).
#include <doctest/doctest.h>

#include "Rendering/BlockTextures.h"
#include "World/LastlightTerrain.h"

#include <map>

using namespace zodar::voxel;

namespace {

class Fnv {
public:
    void bytes(const void* data, size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) m_h = (m_h ^ p[i]) * 0x100000001B3ull;
    }
    template <class T> void value(const T& v) { bytes(&v, sizeof(T)); }
    uint64_t get() const { return m_h; }

private:
    uint64_t m_h = 0xCBF29CE484222325ull;
};

uint64_t highlandsHash(const lastlight::LastlightTerrain& terrain, const VoxelPalette& palette, const ChunkCoord& center) {
    std::map<ChunkCoord, VoxelChunk> chunks;
    ChunkNeighborhood nb;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const ChunkCoord c = center.offset(dx, dy, dz);
                terrain.generate(c, chunks[c]);
            }
        }
    }
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                nb.chunks[ChunkNeighborhood::slot(dx, dy, dz)] = &chunks.at(center.offset(dx, dy, dz));
            }
        }
    }
    Fnv h;
    for (int lod = 0; lod <= 3; ++lod) {
        MeshOptions o;
        o.lod = lod;
        const zodar::MeshData mesh = meshChunk(nb, palette, o);
        h.value(static_cast<uint64_t>(mesh.vertices.size()));
        h.value(static_cast<uint64_t>(mesh.indices.size()));
        if (!mesh.vertices.empty()) h.bytes(mesh.vertices.data(), mesh.vertices.size() * sizeof(zodar::Vertex));
        if (!mesh.indices.empty()) h.bytes(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
    }
    return h.get();
}

} // namespace

TEST_CASE("highlands: cube-only mesher output matches the legacy golden hashes") {
    const lastlight::LastlightTerrain terrain(1337);
    VoxelPalette palette = lastlight::makePalette();
    lastlight::assignFaceLayers(palette);
    const VoxelPalette untextured = lastlight::makePalette();
    CHECK(highlandsHash(terrain, palette, {0, 0, 0}) == 0x75ABAD1BD3953923ull);
    CHECK(highlandsHash(terrain, palette, {3, 1, -2}) == 0xB9B23F3A46FD0825ull);
    CHECK(highlandsHash(terrain, palette, {-5, -1, 4}) == 0x3CE880FCD6C9E9F5ull);
    CHECK(highlandsHash(terrain, untextured, {0, 0, 0}) == 0xF858E10ACEBF497Dull);
}
