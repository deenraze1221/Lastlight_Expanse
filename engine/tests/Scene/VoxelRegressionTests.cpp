// Cube-only mesher regression (Phase V1): golden hashes of meshChunk / VoxelWorld output for
// fixed synthetic inputs, captured on the pre-V1 mesher. Any change of these hashes means the
// legacy cube path is no longer byte-identical (docs/guide/VOXELS.md §4).
//
// Inputs are built from integer hashing only (no floating-point noise), so the hashes do not
// depend on libm. Output floats come from single IEEE operations on exact values.
#include <doctest/doctest.h>

#include "Scene/Voxel/ChunkMesher.h"
#include "Scene/Voxel/VoxelWorld.h"
#include "../Support/MeshHash.h"

#include <chrono>
#include <cstring>
#include <map>
#include <memory>

using namespace zodar;
using namespace zodar::voxel;
using zodar::test::Fnv;
using zodar::test::hashMesh;
using zodar::test::meshHash;

namespace {

uint64_t splitmix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

uint64_t cellHash(int x, int y, int z, uint64_t seed) {
    return splitmix(seed ^ splitmix(static_cast<uint32_t>(x) ^ splitmix(static_cast<uint32_t>(y) ^
                                                                        splitmix(static_cast<uint32_t>(z)))));
}

// Rolling terrain from integer hashing: heights per column, a few holes (caves), 4 materials.
class HashTerrain final : public IVoxelGenerator {
public:
    explicit HashTerrain(uint64_t seed) : m_seed(seed) {}
    int height(int x, int z) const {
        // Smooth-ish: average of hashed heights on a coarse 8-voxel lattice plus jitter.
        const int cx = x >> 3;
        const int cz = z >> 3;
        int sum = 0;
        for (int dz = 0; dz <= 1; ++dz) {
            for (int dx = 0; dx <= 1; ++dx) {
                sum += static_cast<int>(cellHash(cx + dx, 0, cz + dz, m_seed) % 40);
            }
        }
        return sum / 4 + static_cast<int>(cellHash(x, 1, z, m_seed) % 3) - 4;
    }
    void generate(const ChunkCoord& coord, VoxelChunk& out) const override {
        const glm::ivec3 o = coord.originVoxel();
        for (int z = 0; z < kChunkSize; ++z) {
            for (int x = 0; x < kChunkSize; ++x) {
                const int h = height(o.x + x, o.z + z);
                for (int y = 0; y < kChunkSize; ++y) {
                    const int wy = o.y + y;
                    Voxel v = kEmpty;
                    if (wy < h) {
                        v = wy < h - 6 ? Voxel(1) : Voxel(2);
                        if (wy == h - 1) v = Voxel(3 + static_cast<Voxel>(cellHash(o.x + x, 2, o.z + z, m_seed) % 2));
                        if (cellHash(o.x + x, wy, o.z + z, m_seed ^ 0xCAFEull) % 23 == 0) v = kEmpty; // holes
                    }
                    if (v != kEmpty) out.set(x, y, z, v);
                }
            }
        }
        out.compact();
    }

private:
    uint64_t m_seed;
};

VoxelPalette legacyPalette(bool layers) {
    VoxelPalette p;
    p.colors = {glm::vec4(0), glm::vec4(0.3f, 0.3f, 0.32f, 1), glm::vec4(0.4f, 0.25f, 0.1f, 1),
                glm::vec4(0.2f, 0.5f, 0.1f, 1), glm::vec4(0.6f, 0.6f, 0.55f, 1)};
    if (layers) p.faceLayers = {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 4, 2}, {5, 5, 6}};
    return p;
}

// 27 generated chunks around `center`, owned by the returned map.
struct GeneratedBlock {
    std::map<ChunkCoord, VoxelChunk> chunks;
    ChunkNeighborhood nb;
};

std::unique_ptr<GeneratedBlock> generateBlock(const IVoxelGenerator& gen, const ChunkCoord& center) {
    auto block = std::make_unique<GeneratedBlock>();
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const ChunkCoord c = center.offset(dx, dy, dz);
                gen.generate(c, block->chunks[c]);
            }
        }
    }
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                block->nb.chunks[ChunkNeighborhood::slot(dx, dy, dz)] = &block->chunks.at(center.offset(dx, dy, dz));
            }
        }
    }
    return block;
}

// Meshes `nb` at every LOD with a set of option variants and hashes everything in order.
uint64_t sweepHash(const ChunkNeighborhood& nb, const VoxelPalette& palette) {
    Fnv h;
    for (int lod = 0; lod <= kMaxLod; ++lod) {
        for (int variant = 0; variant < 4; ++variant) {
            MeshOptions o;
            o.lod = lod;
            o.voxelSize = variant == 2 ? 0.5f : 1.0f;
            o.ambientOcclusion = variant != 1;
            o.sealFaces = variant == 3 ? uint8_t(0x2D) : uint8_t(0);
            MeshStats stats;
            hashMesh(h, meshChunk(nb, palette, o, &stats));
            h.value(stats.quads);
            h.value(stats.faces);
        }
    }
    return h.get();
}

} // namespace

TEST_CASE("voxel regression: cube-only mesher output matches the legacy golden hashes") {
    SUBCASE("single voxel and small shapes") {
        VoxelChunk c;
        c.set(3, 3, 3, 1);
        c.set(10, 0, 10, 2);
        c.set(11, 0, 10, 2);
        c.set(10, 1, 10, 3);
        c.set(31, 31, 31, 4);
        c.set(0, 0, 0, 4);
        ChunkNeighborhood nb;
        nb.chunks[13] = &c;
        CHECK(sweepHash(nb, legacyPalette(false)) == 0xCC5F3E53ADF8F4B5ull);
        CHECK(sweepHash(nb, legacyPalette(true)) == 0x5B6411EA5048B835ull);
    }
    SUBCASE("hashed terrain with neighbours") {
        const HashTerrain gen(7);
        const auto surface = generateBlock(gen, {0, 0, 0});
        CHECK(sweepHash(surface->nb, legacyPalette(false)) == 0x79BC56FD93202444ull);
        CHECK(sweepHash(surface->nb, legacyPalette(true)) == 0x37C0B9B68985A61Cull);
        const auto deep = generateBlock(gen, {1, -1, -2}); // mostly solid with holes
        CHECK(sweepHash(deep->nb, legacyPalette(true)) == 0x8197D82B05E68D6Full);
    }
    SUBCASE("ids outside the palette keep the fallback colour") {
        VoxelChunk c;
        c.set(5, 5, 5, 9);
        c.set(6, 5, 5, 300);
        ChunkNeighborhood nb;
        nb.chunks[13] = &c;
        CHECK(sweepHash(nb, legacyPalette(true)) == 0x0B06C76F97A1B225ull);
    }
}

TEST_CASE("voxel regression: streamed cube-only world matches the legacy golden hash") {
    StreamingSettings s;
    s.viewRadius = 3;
    s.minChunkY = -1;
    s.maxChunkY = 1;
    s.lodDistances = {1.5f, 2.5f};
    s.maxJobsPerUpdate = 1000;
    s.maxJobsInFlight = 1000;
    VoxelWorld world(std::make_shared<HashTerrain>(11), legacyPalette(true), s);
    std::map<ChunkCoord, std::pair<int, uint64_t>> live;
    const glm::vec3 observer{16.0f, 20.0f, 16.0f};
    for (int i = 0; i < 64 && (i == 0 || !world.isSettled()); ++i) {
        world.update(observer);
        world.finishPendingWork();
        for (const ChunkMeshEvent& e : world.takeMeshEvents()) {
            if (e.type == ChunkMeshEvent::Type::Remove) live.erase(e.coord);
            else live[e.coord] = {e.lod, meshHash(e.mesh)};
        }
    }
    REQUIRE(world.isSettled());
    Fnv h;
    for (const auto& [coord, value] : live) {
        h.value(coord.x);
        h.value(coord.y);
        h.value(coord.z);
        h.value(value.first);
        h.value(value.second);
    }
    CHECK(live.size() == 54u);
    CHECK(h.get() == 0x9857B6DD56D4405Dull);
}

// Timing of the cube-only mesher (skipped by default). Run in Release:
//   ZodarTests --no-skip -tc='voxel bench*'
TEST_CASE("voxel bench: cube-only meshChunk" * doctest::skip()) {
    const HashTerrain gen(7);
    std::vector<std::unique_ptr<GeneratedBlock>> blocks;
    for (int i = 0; i < 6; ++i) blocks.push_back(generateBlock(gen, {i, i % 2 - 1, -i}));
    const VoxelPalette palette = legacyPalette(true);
    uint64_t guard = 0;
    for (int lod = 0; lod <= 2; ++lod) {
        MeshOptions o;
        o.lod = lod;
        double best = 1e30;
        for (int rep = 0; rep < 7; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < 4; ++k) {
                for (const auto& b : blocks) guard += meshChunk(b->nb, palette, o).vertices.size();
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            best = std::min(best, ms / (4.0 * static_cast<double>(blocks.size())));
        }
        MESSAGE("lod ", lod, ": ", best, " ms per chunk (best of 7)");
    }
    CHECK(guard > 0);
}
