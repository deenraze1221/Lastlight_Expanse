// Voxel LOD with shapes (Phase V3, docs/guide/VOXELS.md §5).
//
// Three kinds of tests (V3 plan):
//   1. regression: with default settings the streamed mesh output of shape worlds matches the
//      golden hash recorded on the pre-V3 code (commit 27e7788): V3 changes no LOD choice or mesh;
//   2. new behaviour: conservative LOD for structural chunks;
//   3. known limitations: characterisation tests that pin today's behaviour (documented in
//      VOXELS §5, not fixed in V3).
#include <doctest/doctest.h>

#include "Scene/Voxel/ChunkMesher.h"
#include "Scene/Voxel/VoxelWorld.h"
#include "../Support/MeshHash.h"
#include "../Support/VoxelTestWorlds.h"

#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <vector>

using namespace zodar;
using namespace zodar::voxel;
using zodar::test::Fnv;
using zodar::test::meshHash;
using zodar::test::QueryTerrain;
using zodar::test::ShapeRegistry;

namespace {

struct LiveMesh {
    int lod = 0;
    glm::vec3 origin{0.0f};
    MeshData mesh;
};

std::map<ChunkCoord, LiveMesh> settleLive(VoxelWorld& world, const glm::vec3& observer,
                                          std::map<ChunkCoord, LiveMesh> live = {}) {
    for (int i = 0; i < 64 && (i == 0 || !world.isSettled()); ++i) {
        world.update(observer);
        world.finishPendingWork();
        for (ChunkMeshEvent& e : world.takeMeshEvents()) {
            if (e.type == ChunkMeshEvent::Type::Remove) live.erase(e.coord);
            else live[e.coord] = {e.lod, e.origin, std::move(e.mesh)};
        }
    }
    REQUIRE(world.isSettled());
    return live;
}

uint64_t liveHash(const std::map<ChunkCoord, LiveMesh>& live) {
    Fnv h;
    for (const auto& [coord, m] : live) {
        h.value(coord.x);
        h.value(coord.y);
        h.value(coord.z);
        h.value(m.lod);
        h.value(meshHash(m.mesh));
    }
    return h.get();
}

// Ray against every triangle of the live meshes (double precision, test-only reference).
bool meshHit(const std::map<ChunkCoord, LiveMesh>& live, const glm::dvec3& o, const glm::dvec3& d) {
    for (const auto& [coord, m] : live) {
        const auto& v = m.mesh.vertices;
        const auto& idx = m.mesh.indices;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const glm::dvec3 a = glm::dvec3(v[idx[i]].position + m.origin);
            const glm::dvec3 b = glm::dvec3(v[idx[i + 1]].position + m.origin);
            const glm::dvec3 c = glm::dvec3(v[idx[i + 2]].position + m.origin);
            const glm::dvec3 e1 = b - a;
            const glm::dvec3 e2 = c - a;
            const glm::dvec3 p = glm::cross(d, e2);
            const double det = glm::dot(e1, p);
            if (std::abs(det) < 1e-12) continue;
            const double inv = 1.0 / det;
            const glm::dvec3 s = o - a;
            const double u = glm::dot(s, p) * inv;
            if (u < -1e-9 || u > 1.0 + 1e-9) continue;
            const glm::dvec3 q = glm::cross(s, e1);
            const double w = glm::dot(d, q) * inv;
            if (w < -1e-9 || u + w > 1.0 + 1e-9) continue;
            const double t = glm::dot(e2, q) * inv;
            if (t >= 0.0 && t <= 1.0) return true;
        }
    }
    return false;
}

// Seam probe: for every pair of horizontally adjacent live chunks with different LODs, rays
// from high above one side to deep below the other side cross the seam. A watertight surface
// over solid ground stops every such ray. Returns {rays, misses}.
std::pair<int, int> probeSeams(const std::map<ChunkCoord, LiveMesh>& live, float topY, float bottomY) {
    int rays = 0;
    int misses = 0;
    for (const auto& [coord, m] : live) {
        for (int axis : {0, 2}) {
            ChunkCoord n = coord;
            (axis == 0 ? n.x : n.z) += 1;
            const auto it = live.find(n);
            if (it == live.end() || it->second.lod == m.lod || coord.y != 0) continue;
            const float seam = it->second.origin[axis];
            const int other = 2 - axis;
            for (int k = 0; k < 16; ++k) {
                for (int side : {-1, 1}) {
                    glm::dvec3 from(0.0), to(0.0);
                    from[axis] = seam - side * 3.0;
                    to[axis] = seam + side * 3.0;
                    from[other] = m.origin[other] + k * 2.0 + 0.5;
                    to[other] = from[other] + 0.37;
                    from.y = topY;
                    to.y = bottomY;
                    ++rays;
                    if (!meshHit(live, from, to - from)) ++misses;
                }
            }
        }
    }
    return {rays, misses};
}

} // namespace

TEST_CASE("voxel LOD regression: streamed shape worlds match the pre-V3 golden hash") {
    const ShapeRegistry reg;
    VoxelWorld world(std::make_shared<QueryTerrain>(7, QueryTerrain::Mode::Shapes, reg.shapeCount(), reg.noCollisionOffset),
                     reg.palette, test::queryWorldSettings(1.0f));
    const auto live = settleLive(world, {8, 8, 8});
    std::map<int, int> lods;
    for (const auto& [c, m] : live) lods[m.lod]++;
    MESSAGE("chunks ", live.size(), " lod0 ", lods[0], " lod1 ", lods[1], " lod2 ", lods[2], " hash ",
            test::hex64(liveHash(live)));
    CHECK(lods[0] > 0); // the world spans LOD 0, 1 and 2
    CHECK(lods[1] > 0);
    CHECK(lods[2] > 0);
    CHECK(liveHash(live) == 0x1823C9036434EA3Aull);
}

TEST_CASE("voxel LOD known limitation: one-cell walls vanish from LOD 2 (majority rule)") {
    VoxelChunk c;
    for (int y = 0; y < kChunkSize; ++y) {
        for (int z = 0; z < kChunkSize; ++z) c.set(8, y, z, 1);
    }
    ChunkNeighborhood nb;
    nb.chunks[13] = &c;
    const VoxelPalette p = test::legacyTestPalette();
    MeshOptions o;
    o.lod = 0;
    CHECK_FALSE(meshChunk(nb, p, o).indices.empty());
    o.lod = 1; // 4 of 8 voxels: a half-full cell stays solid
    CHECK_FALSE(meshChunk(nb, p, o).indices.empty());
    o.lod = 2; // 16 of 64 voxels: the wall disappears (VOXELS §5, known limitation)
    CHECK(meshChunk(nb, p, o).indices.empty());
}

TEST_CASE("voxel LOD seams: rays across LOD borders of a shape world hit the surface") {
    const ShapeRegistry reg;
    VoxelWorld world(std::make_shared<QueryTerrain>(7, QueryTerrain::Mode::Shapes, reg.shapeCount(), reg.noCollisionOffset),
                     reg.palette, test::queryWorldSettings(1.0f));
    const auto live = settleLive(world, {8, 8, 8});
    const auto [rays, misses] = probeSeams(live, 30.0f, -20.0f);
    MESSAGE("seam rays ", rays, " misses ", misses);
    CHECK(rays > 0);
    CHECK(misses == 0);
}
