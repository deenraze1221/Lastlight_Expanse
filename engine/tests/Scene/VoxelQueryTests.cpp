// Voxel queries (Phase V3, docs/guide/VOXELS.md §6): raycasts and solid-box queries.
//
// Golden raycast tables: rays from integer hashing (exact float inputs) through hashed worlds.
// The legacy and cube-registry tables were recorded on the pre-V3 raycast (commit 27e7788) and
// must stay bit-identical (cell, normal, distance bits, value): cube cells keep the original
// traversal exactly.
#include <doctest/doctest.h>

#include "Scene/Voxel/VoxelWorld.h"
#include "../Support/MeshHash.h"
#include "../Support/VoxelTestWorlds.h"

#include <bit>
#include <cstring>
#include <memory>
#include <vector>

using namespace zodar;
using namespace zodar::voxel;
using zodar::test::Fnv;
using zodar::test::hash3;
using zodar::test::QueryTerrain;
using zodar::test::ShapeRegistry;

namespace {

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
    float maxDistance;
};

// Exact small rationals: k / 16 positions, components in [-1, 1] at 1/64 steps.
float frac(uint64_t r, int range, float scale, float offset) {
    return static_cast<float>(static_cast<int>(r % static_cast<uint64_t>(range))) * scale + offset;
}

// `scale` = voxel size: rays are generated in voxel units and scaled exactly (powers of two).
std::vector<Ray> testRays(float scale) {
    std::vector<Ray> rays;
    for (int i = 0; i < 512; ++i) {
        const uint64_t a = hash3(i, 0, 0, 99);
        const uint64_t b = hash3(i, 1, 0, 99);
        glm::vec3 o{frac(a, 1024, 1.0f / 16.0f, -24.0f), frac(a >> 16, 512, 1.0f / 16.0f, -6.0f),
                    frac(a >> 32, 1024, 1.0f / 16.0f, -24.0f)};
        glm::vec3 d{frac(b, 129, 1.0f / 64.0f, -1.0f), frac(b >> 16, 129, 1.0f / 64.0f, -1.0f),
                    frac(b >> 32, 129, 1.0f / 64.0f, -1.0f)};
        if (i % 4 == 0) d.y = -std::abs(d.y) - 0.25f; // many rays toward the ground
        rays.push_back({o * scale, d, 48.0f * scale});
    }
    // Structured rays: axis-aligned and diagonal rays from cell corners, cell faces and centres.
    const glm::vec3 dirs[] = {{1, 0, 0}, {-1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 1},   {0, 0, -1},
                              {1, -1, 0}, {1, -1, 1}, {-1, -1, -1}, {0, -1, 1}, {2, -1, 0}, {1, -2, 3}};
    const glm::vec3 offsets[] = {{0, 0, 0}, {0.5f, 0.5f, 0.5f}, {0.5f, 0, 0.5f}, {0, 0.5f, 0}, {0.25f, 0.75f, 0.125f}};
    for (int k = 0; k < 6; ++k) {
        for (const glm::vec3& off : offsets) {
            for (const glm::vec3& d : dirs) {
                const glm::vec3 base{float(k * 5 - 12), float(8 + k), float(7 - k * 3)};
                rays.push_back({(base + off) * scale, d, 40.0f * scale});
            }
        }
    }
    return rays;
}

// Legacy RaycastHit fields only (voxel, normal, distance bits, value).
uint64_t tableHash(const VoxelWorld& world, const std::vector<Ray>& rays, int* hits = nullptr) {
    Fnv h;
    int count = 0;
    for (const Ray& r : rays) {
        const auto hit = world.raycast(r.origin, r.direction, r.maxDistance);
        h.value(uint8_t(hit ? 1 : 0));
        if (!hit) continue;
        ++count;
        h.value(hit->voxel);
        h.value(hit->normal);
        h.value(std::bit_cast<uint32_t>(hit->distance));
        h.value(hit->value);
    }
    if (hits) *hits = count;
    return h.get();
}

} // namespace

TEST_CASE("voxel raycast: legacy and cube-registry results match the pre-V3 golden tables") {
    const glm::vec3 observer{8, 8, 8};
    SUBCASE("legacy palette, 1 m cells") {
        VoxelWorld world(std::make_shared<QueryTerrain>(5, QueryTerrain::Mode::Legacy), test::legacyTestPalette(),
                         test::queryWorldSettings(1.0f));
        test::settleWorld(world, observer);
        int hits = 0;
        const uint64_t h = tableHash(world, testRays(1.0f), &hits);
        MESSAGE("legacy hits ", hits, " hash ", test::hex64(h));
        CHECK(h == 0x3A17179597B60B05ull);
    }
    SUBCASE("registry cubes incl. kNoCollision cubes, 0.5 m cells") {
        const ShapeRegistry reg;
        VoxelWorld world(std::make_shared<QueryTerrain>(6, QueryTerrain::Mode::RegistryCubes), reg.palette,
                         test::queryWorldSettings(0.5f));
        test::settleWorld(world, observer * 0.5f);
        int hits = 0;
        const uint64_t h = tableHash(world, testRays(0.5f), &hits);
        MESSAGE("registry cube hits ", hits, " hash ", test::hex64(h));
        CHECK(h == 0x476C840510DCF433ull);
    }
}

TEST_CASE("voxel raycast: shape-world table (pre-V3 treats shape cells as full cells)") {
    const ShapeRegistry reg;
    VoxelWorld world(std::make_shared<QueryTerrain>(7, QueryTerrain::Mode::Shapes, reg.shapeCount(), reg.noCollisionOffset),
                     reg.palette, test::queryWorldSettings(1.0f));
    test::settleWorld(world, {8, 8, 8});
    int hits = 0;
    const uint64_t h = tableHash(world, testRays(1.0f), &hits);
    MESSAGE("shape hits ", hits, " hash ", test::hex64(h));
    CHECK(h == 0x4A0545B69CF5FA87ull);
}
