// Voxel queries (Phase V3, docs/guide/VOXELS.md §6): raycasts and solid-box queries.
//
// Golden raycast tables: rays from integer hashing (exact float inputs) through hashed worlds.
// The legacy and cube-registry tables were recorded on the pre-V3 raycast (commit 27e7788) and
// must stay bit-identical (cell, normal, distance bits, value): cube cells keep the original
// traversal exactly.
#include <doctest/doctest.h>

#include "Core/Error.h"
#include "Core/JobSystem.h"
#include "Scene/Voxel/VoxelWorld.h"
#include "../Support/MeshHash.h"
#include "../Support/VoxelTestWorlds.h"

#include <bit>
#include <cstring>
#include <limits>
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

namespace {

// Sparse hand-placed voxels.
class PlacedGenerator final : public IVoxelGenerator {
public:
    std::vector<std::pair<glm::ivec3, Voxel>> cells;
    void generate(const ChunkCoord& coord, VoxelChunk& out) const override {
        for (const auto& [p, v] : cells) {
            if (chunkOf(p) == coord) {
                const glm::ivec3 l = localOf(p);
                out.set(l.x, l.y, l.z, v);
            }
        }
    }
};

struct PlacedWorld {
    std::shared_ptr<PlacedGenerator> gen = std::make_shared<PlacedGenerator>();
    std::unique_ptr<VoxelWorld> world;
    void build(const VoxelPalette& palette, float voxelSize, JobSystem* jobs = nullptr) {
        world = std::make_unique<VoxelWorld>(gen, palette, test::queryWorldSettings(voxelSize), jobs);
        test::settleWorld(*world, {8.0f * voxelSize, 8.0f * voxelSize, 8.0f * voxelSize});
    }
};

bool sameBox(const SolidBox& a, const SolidBox& b) { return a.min == b.min && a.max == b.max && a.value == b.value; }

std::vector<SolidBox> query(const VoxelWorld& w, glm::vec3 lo, glm::vec3 hi, QueryStatus* status = nullptr) {
    std::vector<SolidBox> out;
    const QueryStatus s = w.collectSolidBoxes({lo, hi}, out);
    if (status) *status = s;
    return out;
}

} // namespace

TEST_CASE("voxel solid boxes: cube runs merge along X, split by value and chunk, clip to the query") {
    const ShapeRegistry reg;
    PlacedWorld pw;
    for (int x = 2; x <= 9; ++x) pw.gen->cells.push_back({{x, 1, 1}, x == 6 ? Voxel(0x7000) : reg.cube});
    for (int x = 28; x <= 35; ++x) pw.gen->cells.push_back({{x, 3, 3}, reg.cube}); // crosses x = 32
    pw.build(reg.palette, 0.5f);
    const VoxelWorld& w = *pw.world;

    QueryStatus st;
    auto boxes = query(w, {0, 0, 0}, {10, 1, 1}, &st);
    CHECK(st == QueryStatus::Complete);
    // Run 2..5 (cube), 6 (unknown id: fallback cube, own run), 7..9.
    REQUIRE(boxes.size() == 3u);
    CHECK(sameBox(boxes[0], {{1.0f, 0.5f, 0.5f}, {3.0f, 1.0f, 1.0f}, reg.cube}));
    CHECK(sameBox(boxes[1], {{3.0f, 0.5f, 0.5f}, {3.5f, 1.0f, 1.0f}, Voxel(0x7000)}));
    CHECK(sameBox(boxes[2], {{3.5f, 0.5f, 0.5f}, {5.0f, 1.0f, 1.0f}, reg.cube}));

    // Clipped to the query's cell range: cells 3..4 only.
    boxes = query(w, {1.6f, 0.6f, 0.6f}, {2.4f, 0.9f, 0.9f});
    REQUIRE(boxes.size() == 1u);
    CHECK(sameBox(boxes[0], {{1.5f, 0.5f, 0.5f}, {2.5f, 1.0f, 1.0f}, reg.cube}));

    // Runs never cross a chunk boundary (x = 32 cells = 16 m at 0.5 m).
    boxes = query(w, {13, 1.5f, 1.5f}, {18, 2, 2});
    REQUIRE(boxes.size() == 2u);
    CHECK(sameBox(boxes[0], {{14.0f, 1.5f, 1.5f}, {16.0f, 2.0f, 2.0f}, reg.cube}));
    CHECK(sameBox(boxes[1], {{16.0f, 1.5f, 1.5f}, {18.0f, 2.0f, 2.0f}, reg.cube}));
}

TEST_CASE("voxel solid boxes: closed intervals, empty queries, appending") {
    const ShapeRegistry reg;
    PlacedWorld pw;
    pw.gen->cells.push_back({{4, 4, 4}, reg.cube}); // [2, 2.5]^3 at 0.5 m
    pw.build(reg.palette, 0.5f);
    const VoxelWorld& w = *pw.world;
    CHECK(query(w, {2.5f, 2.5f, 2.5f}, {3, 3, 3}).size() == 1u);  // touches a corner
    CHECK(query(w, {1, 2.1f, 2.1f}, {2, 2.2f, 2.2f}).size() == 1u); // touches the -X face
    CHECK(query(w, {2.2f, 2.2f, 2.2f}, {2.2f, 2.2f, 2.2f}).size() == 1u); // point query inside
    CHECK(query(w, {2.5001f, 2, 2}, {3, 2.5f, 2.5f}).empty());
    CHECK(query(w, {1, 1, 1}, {1.999f, 3, 3}).empty());
    // Appends, never clears.
    std::vector<SolidBox> out{{glm::vec3(-1), glm::vec3(-1), Voxel(77)}};
    CHECK(w.collectSolidBoxes({{2, 2, 2}, {2.5f, 2.5f, 2.5f}}, out) == QueryStatus::Complete);
    REQUIRE(out.size() == 2u);
    CHECK(out[0].value == 77);
}

TEST_CASE("voxel solid boxes: every shape, rotation and collision flag") {
    const ShapeRegistry reg;
    PlacedWorld pw;
    // One block per id on a sparse grid (two cells apart so queries see one cell each).
    const auto ids = static_cast<Voxel>(reg.palette.blocks.size());
    for (Voxel id = 1; id < ids; ++id) {
        pw.gen->cells.push_back({{(id % 8) * 2 - 6, 2, (id / 8) * 2 - 6}, id});
    }
    pw.build(reg.palette, 0.5f);
    const VoxelWorld& w = *pw.world;
    const BlockTable& table = w.blocks();
    int checked = 0;
    for (Voxel id = 1; id < ids; ++id) {
        CAPTURE(id);
        const glm::vec3 cell(float((id % 8) * 2 - 6), 2.0f, float((id / 8) * 2 - 6));
        const auto boxes = query(w, cell * 0.5f, (cell + 1.0f) * 0.5f);
        const bool noCollision = (reg.palette.blocks[id].flags & BlockState::kNoCollision) != 0;
        if (noCollision) {
            CHECK(boxes.empty());
            continue;
        }
        const Shape* shape = table.shape(id);
        const std::vector<ShapeBox> expected =
            shape ? shape->collision : std::vector<ShapeBox>{{glm::vec3(0.0f), glm::vec3(1.0f)}};
        REQUIRE(boxes.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            CHECK(sameBox(boxes[i], {(cell + expected[i].min) * 0.5f, (cell + expected[i].max) * 0.5f, id}));
        }
        // The rotated boxes are the base shape's boxes turned by the block's yaw steps.
        if (shape) {
            const BlockState& b = reg.palette.blocks[id];
            const Shape rotated = rotateShape((*reg.lib)[b.shape], b.rotation);
            REQUIRE(rotated.collision.size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i) {
                CHECK(rotated.collision[i].min == expected[i].min);
                CHECK(rotated.collision[i].max == expected[i].max);
            }
        }
        ++checked;
    }
    CHECK(checked == int(reg.shapeCount()) * 4 + 1);
    // A shape box that does not touch the query is not returned: the wedge's lowest step
    // (z in [0, 1/4]) at rotation 0 is far from a query at the cell's top back corner.
    const Voxel wedge = reg.id("wedge");
    const glm::vec3 cell(float((wedge % 8) * 2 - 6), 2.0f, float((wedge / 8) * 2 - 6));
    const auto top = query(w, (cell + glm::vec3(0.5f, 0.80f, 0.95f)) * 0.5f, (cell + glm::vec3(0.6f, 0.86f, 0.99f)) * 0.5f);
    REQUIRE(top.size() == 1u);
    CHECK(top[0].max.y == (cell.y + 7.0f / 8.0f) * 0.5f);
}

TEST_CASE("voxel solid boxes: missing chunks, world Y range, invalid queries") {
    const ShapeRegistry reg;
    PlacedWorld pw;
    pw.gen->cells.push_back({{-3, -5, -7}, reg.cube}); // negative coordinates
    pw.build(reg.palette, 1.0f);
    const VoxelWorld& w = *pw.world;
    QueryStatus st;
    auto boxes = query(w, {-3, -5, -7}, {-2.5f, -4.5f, -6.5f}, &st);
    CHECK(st == QueryStatus::Complete);
    REQUIRE(boxes.size() == 1u);
    CHECK(sameBox(boxes[0], {{-3, -5, -7}, {-2, -4, -6}, reg.cube}));

    // Query from the loaded cell to far beyond the streaming radius: partial result.
    boxes = query(w, {-3, -5, -7}, {400, -4.5f, -6.5f}, &st);
    CHECK(st == QueryStatus::MissingChunks);
    REQUIRE(boxes.size() == 1u); // the loaded chunk's box is still there
    CHECK(sameBox(boxes[0], {{-3, -5, -7}, {-2, -4, -6}, reg.cube}));

    // Above and below the world's chunk range: empty like getVoxel, not missing.
    CHECK(query(w, {0, 200, 0}, {4, 300, 4}, &st).empty());
    CHECK(st == QueryStatus::Complete);
    CHECK(query(w, {0, -300, 0}, {4, -200, 4}, &st).empty());
    CHECK(st == QueryStatus::Complete);

    std::vector<SolidBox> out;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS(w.collectSolidBoxes({{1, 0, 0}, {0, 1, 1}}, out), zodar::Error);
    CHECK_THROWS_AS(w.collectSolidBoxes({{nan, 0, 0}, {1, 1, 1}}, out), zodar::Error);
    CHECK_THROWS_AS(w.collectSolidBoxes({{0, 0, 0}, {inf, 1, 1}}, out), zodar::Error);
    CHECK_THROWS_AS(w.collectSolidBoxes({{0, 0, 0}, {1e12f, 1, 1}}, out), zodar::Error);
    CHECK(out.empty());
}

TEST_CASE("voxel solid boxes: hashed shape world agrees with getVoxel and is deterministic") {
    const ShapeRegistry reg;
    const auto gen = std::make_shared<QueryTerrain>(7, QueryTerrain::Mode::Shapes, reg.shapeCount(), reg.noCollisionOffset);
    VoxelWorld world(gen, reg.palette, test::queryWorldSettings(0.5f));
    test::settleWorld(world, {4, 4, 4});
    JobSystem jobs(3);
    VoxelWorld threaded(gen, reg.palette, test::queryWorldSettings(0.5f), &jobs);
    test::settleWorld(threaded, {4, 4, 4});
    const BlockTable& table = world.blocks();
    for (int q = 0; q < 64; ++q) {
        const uint64_t r = hash3(q, 5, 5, 3);
        const glm::ivec3 lo(int(r % 40) - 20, int((r >> 8) % 16) - 4, int((r >> 16) % 40) - 20);
        const glm::ivec3 size(int((r >> 24) % 6) + 1, int((r >> 32) % 6) + 1, int((r >> 40) % 6) + 1);
        // Query strictly inside the cell range [lo, lo + size) so neighbours are not touched.
        const glm::vec3 qmin = (glm::vec3(lo) + 0.01f) * 0.5f;
        const glm::vec3 qmax = (glm::vec3(lo + size) - 0.01f) * 0.5f;
        QueryStatus st;
        const auto boxes = query(world, qmin, qmax, &st);
        CHECK(st == QueryStatus::Complete);
        const auto again = query(threaded, qmin, qmax);
        REQUIRE(again.size() == boxes.size());
        for (size_t i = 0; i < boxes.size(); ++i) CHECK(sameBox(again[i], boxes[i]));
        // Reference from getVoxel: cube cells (count) and shape boxes touching the query.
        int cubeCells = 0;
        size_t shapeBoxes = 0;
        for (int y = lo.y; y < lo.y + size.y; ++y) {
            for (int z = lo.z; z < lo.z + size.z; ++z) {
                for (int x = lo.x; x < lo.x + size.x; ++x) {
                    const Voxel v = world.getVoxel({x, y, z});
                    if (!table.collides(v)) continue;
                    if (table.isCube(v)) {
                        ++cubeCells;
                        continue;
                    }
                    for (const ShapeBox& b : table.shape(v)->collision) {
                        const glm::vec3 bmin = (glm::vec3(x, y, z) + b.min) * 0.5f;
                        const glm::vec3 bmax = (glm::vec3(x, y, z) + b.max) * 0.5f;
                        if (glm::all(glm::lessThanEqual(bmin, qmax)) && glm::all(glm::greaterThanEqual(bmax, qmin))) {
                            ++shapeBoxes;
                        }
                    }
                }
            }
        }
        int gotCubeCells = 0;
        size_t gotShapeBoxes = 0;
        for (const SolidBox& b : boxes) {
            CHECK(table.collides(b.value));
            if (table.isCube(b.value)) {
                CHECK(b.max.y - b.min.y == 0.5f);
                CHECK(b.max.z - b.min.z == 0.5f);
                gotCubeCells += int((b.max.x - b.min.x) / 0.5f + 0.5f);
            } else {
                ++gotShapeBoxes;
            }
        }
        CHECK(gotCubeCells == cubeCells);
        CHECK(gotShapeBoxes == shapeBoxes);
    }
}
