#include <doctest/doctest.h>

#include "Core/JobSystem.h"
#include "Scene/Noise.h"
#include "Scene/Voxel/ChunkMesher.h"
#include "Scene/Voxel/VoxelWorld.h"

#include <atomic>
#include <map>

using namespace zodar;
using namespace zodar::voxel;

namespace {

VoxelPalette palette() { return {{glm::vec4(0), glm::vec4(0.5f, 0.5f, 0.5f, 1), glm::vec4(0.2f, 0.8f, 0.2f, 1)}}; }

// Flat ground: solid (id 1) below world y = height, id 2 on the top layer.
class FlatGenerator final : public IVoxelGenerator {
public:
    explicit FlatGenerator(int height) : m_height(height) {}
    void generate(const ChunkCoord& coord, VoxelChunk& out) const override {
        for (int y = 0; y < kChunkSize; ++y) {
            const int wy = coord.y * kChunkSize + y;
            out.fillLayer(y, wy < m_height - 1 ? Voxel(1) : wy == m_height - 1 ? Voxel(2) : kEmpty);
        }
    }

private:
    int m_height;
};

ChunkNeighborhood single(const VoxelChunk& c) {
    ChunkNeighborhood nb;
    nb.chunks[13] = &c;
    return nb;
}

} // namespace

TEST_CASE("voxel chunk: uniform fast path and compaction") {
    VoxelChunk c;
    CHECK(c.isUniform());
    CHECK(c.isEmpty());
    c.set(1, 2, 3, 7);
    CHECK_FALSE(c.isUniform());
    CHECK(c.get(1, 2, 3) == 7);
    CHECK(c.memoryBytes() == kChunkVolume * sizeof(Voxel));
    c.set(1, 2, 3, kEmpty);
    c.compact();
    CHECK(c.isUniform());
    CHECK(c.memoryBytes() == 0);
}

TEST_CASE("voxel coords: floor division for negative voxels") {
    CHECK(chunkOf({-1, 0, 32}) == ChunkCoord{-1, 0, 1});
    CHECK(localOf({-1, 0, 33}) == glm::ivec3(31, 0, 1));
}

TEST_CASE("mesher: single voxel gives 6 quads with outward CCW faces") {
    VoxelChunk c;
    c.set(4, 4, 4, 1);
    MeshStats stats;
    const MeshData mesh = meshChunk(single(c), palette(), {}, &stats);
    CHECK(stats.quads == 6);
    CHECK(mesh.vertices.size() == 24);
    CHECK(mesh.indices.size() == 36);
    CHECK(mesh.validate().empty());
    for (size_t t = 0; t < mesh.indices.size(); t += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[t]];
        const Vertex& b = mesh.vertices[mesh.indices[t + 1]];
        const Vertex& d = mesh.vertices[mesh.indices[t + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, d.position - a.position);
        CHECK(glm::dot(n, a.normal) > 0.0f); // winding matches the normal
        CHECK(glm::dot(a.position - glm::vec3(4.5f), a.normal) > 0.0f); // faces point outward
    }
}

TEST_CASE("mesher: greedy merge of a flat slab") {
    VoxelChunk c;
    for (int z = 0; z < kChunkSize; ++z)
        for (int x = 0; x < kChunkSize; ++x) c.set(x, 0, z, 1);
    MeshStats stats;
    const MeshData mesh = meshChunk(single(c), palette(), {}, &stats);
    CHECK(stats.faces == 32 * 32 * 2 + 32 * 4);
    CHECK(stats.quads == 6); // AO is uniform on an isolated slab: one quad per side
    CHECK(mesh.bounds.min == glm::vec3(0));
    CHECK(mesh.bounds.max == glm::vec3(32, 1, 32));
}

TEST_CASE("mesher: neighbours hide shared faces; sealing exposes them") {
    VoxelChunk full(1);
    ChunkNeighborhood nb;
    for (auto& p : nb.chunks) p = &full;
    CHECK(meshChunk(nb, palette(), {}).indices.empty());
    MeshOptions sealed;
    sealed.sealFaces = 1u << uint8_t(Face::PosY);
    MeshStats stats;
    meshChunk(nb, palette(), sealed, &stats);
    CHECK(stats.faces == 32 * 32);
}

TEST_CASE("mesher: ambient occlusion darkens a corner") {
    VoxelChunk c;
    c.set(5, 5, 5, 1);
    c.set(6, 6, 5, 1); // occludes part of the +Y face of (5,5,5)
    const MeshData mesh = meshChunk(single(c), palette(), {});
    float minShade = 1.0f;
    for (const Vertex& v : mesh.vertices) {
        if (v.normal.y > 0.5f && v.position.y == 6.0f && v.position.x <= 6.0f) minShade = std::min(minShade, v.color.r / 0.5f);
    }
    CHECK(minShade < 0.99f);
}

TEST_CASE("mesher: LOD reduces geometry and keeps the surface height") {
    VoxelChunk c;
    for (int y = 0; y < 16; ++y) c.fillLayer(y, 1);
    for (int lod = 0; lod <= kMaxLod; ++lod) {
        MeshOptions o;
        o.lod = lod;
        const MeshData m = meshChunk(single(c), palette(), o);
        REQUIRE_FALSE(m.indices.empty());
        CHECK(m.bounds.max.y == doctest::Approx(lod == kMaxLod ? 32.0f : 16.0f)); // half-full cell counts as solid
    }
}

TEST_CASE("noise: deterministic and bounded") {
    const GradientNoise a(42), b(42), c(7);
    float maxAbs = 0.0f;
    bool differs = false;
    for (int i = 0; i < 2000; ++i) {
        const glm::vec2 p(i * 0.37f - 300.0f, i * 0.11f + 5.0f);
        CHECK(a.sample(p) == b.sample(p));
        differs |= a.sample(p) != c.sample(p);
        maxAbs = std::max(maxAbs, std::abs(a.fbm(glm::vec3(p, i * 0.05f), 5)));
    }
    CHECK(differs);
    CHECK(maxAbs <= 1.05f);
}

TEST_CASE("job system: runs every job, inline mode works") {
    for (uint32_t workers : {0u, 3u}) {
        JobSystem jobs(workers);
        std::atomic<int> count{0};
        for (int i = 0; i < 500; ++i) jobs.submit([&] { count.fetch_add(1); });
        jobs.waitIdle();
        CHECK(count.load() == 500);
        CHECK(jobs.pendingJobs() == 0);
    }
}

namespace {

StreamingSettings smallWorld() {
    StreamingSettings s;
    s.viewRadius = 3;
    s.minChunkY = -1;
    s.maxChunkY = 1;
    s.lodDistances = {1.5f, 3.0f};
    s.maxJobsPerUpdate = 1000;
    return s;
}

// Applies events into a coord -> lod map, checking event consistency.
void apply(std::map<ChunkCoord, int>& live, std::vector<ChunkMeshEvent> events) {
    for (const auto& e : events) {
        if (e.type == ChunkMeshEvent::Type::Upsert) {
            CHECK_FALSE(e.mesh.indices.empty());
            live[e.coord] = e.lod;
        } else {
            CHECK(live.erase(e.coord) == 1);
        }
    }
}

std::map<ChunkCoord, int> settle(VoxelWorld& world, glm::vec3 observer, std::map<ChunkCoord, int> live = {}) {
    int i = 0;
    do { // at least one update: the observer may have moved since the world last settled
        world.update(observer);
        world.finishPendingWork();
        apply(live, world.takeMeshEvents());
    } while (!world.isSettled() && ++i < 64);
    // A settled world must produce no more events.
    world.update(observer);
    apply(live, world.takeMeshEvents());
    const VoxelWorldStats st = world.stats();
    INFO("data ", st.chunksWithData, " meshed ", st.chunksMeshed, " gen ", st.generateInFlight, " mesh ", st.meshInFlight,
         " total meshed ", st.meshed, " stale ", st.staleMeshesDropped);
    CHECK(world.isSettled());
    return live;
}

} // namespace

TEST_CASE("voxel world: streams, LODs and unloads around the observer") {
    VoxelWorld world(std::make_shared<FlatGenerator>(5), palette(), smallWorld());
    auto live = settle(world, {16, 8, 16});
    // Flat ground at y = 5: only the y = 0 chunk layer has surface geometry.
    REQUIRE_FALSE(live.empty());
    for (const auto& [coord, lod] : live) {
        CHECK(coord.y == 0);
        const float h = glm::length(glm::vec2(coord.x + 0.5f, coord.z + 0.5f) - glm::vec2(0.5f));
        CHECK(h <= 3.0f);
    }
    CHECK(live.at({0, 0, 0}) == 0);
    CHECK(live.at({3, 0, 0}) == 2);
    CHECK(world.getVoxel({0, 4, 0}) == 2);
    CHECK(world.getVoxel({0, 5, 0}) == kEmpty);

    // Move far away: old meshes are removed, new ones appear.
    live = settle(world, {16 + 32 * 20, 8, 16}, live);
    for (const auto& [coord, lod] : live) CHECK(coord.x >= 16);
    CHECK(world.stats().chunksMeshed == live.size());
}

TEST_CASE("voxel world: edits rebuild meshes, survive unloading and raycasts hit them") {
    VoxelWorld world(std::make_shared<FlatGenerator>(5), palette(), smallWorld());
    auto live = settle(world, {16, 8, 16});
    const auto hit = world.raycast({10.5f, 20.0f, 10.5f}, {0, -1, 0}, 50.0f);
    REQUIRE(hit);
    CHECK(hit->voxel == glm::ivec3(10, 4, 10));
    CHECK(hit->normal == glm::ivec3(0, 1, 0));
    CHECK(hit->distance == doctest::Approx(15.0f));

    CHECK(world.setVoxel({10, 5, 10}, 1)); // place on top
    CHECK_FALSE(world.isSettled());
    world.update({16, 8, 16});
    world.finishPendingWork();
    const auto events = world.takeMeshEvents();
    CHECK_FALSE(events.empty());
    apply(live, events);
    live = settle(world, {16, 8, 16}, live);
    CHECK(world.raycast({10.5f, 20.0f, 10.5f}, {0, -1, 0}, 50.0f)->voxel == glm::ivec3(10, 5, 10));

    live = settle(world, {16 + 32 * 20, 8, 16}, live); // unload
    CHECK(world.getVoxel({10, 5, 10}) == kEmpty);
    live = settle(world, {16, 8, 16}, live);          // reload: edit restored
    CHECK(world.getVoxel({10, 5, 10}) == 1);
    CHECK(world.stats().editedChunks == 1);
}

TEST_CASE("voxel world: worker threads produce the same meshes as inline execution") {
    VoxelWorld inlineWorld(std::make_shared<FlatGenerator>(37), palette(), smallWorld());
    const auto expected = settle(inlineWorld, {40, 40, -20});
    JobSystem jobs(4);
    VoxelWorld threaded(std::make_shared<FlatGenerator>(37), palette(), smallWorld(), &jobs);
    CHECK(settle(threaded, {40, 40, -20}) == expected);
}

TEST_CASE("mesher: face texture layers go into vertex alpha (top / side / bottom)") {
    VoxelChunk c;
    c.set(3, 3, 3, 1);
    VoxelPalette p = palette();
    p.faceLayers = {{0, 0, 0}, {7, 8, 1009}};
    const MeshData mesh = meshChunk(single(c), p, {});
    for (const Vertex& v : mesh.vertices) {
        const float expected = v.normal.y > 0.5f ? 7.0f : v.normal.y < -0.5f ? 1009.0f : 8.0f;
        CHECK(v.color.a == expected);
    }
}
