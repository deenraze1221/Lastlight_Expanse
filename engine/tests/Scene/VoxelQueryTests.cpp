// Voxel queries (Phase V3, docs/guide/VOXELS.md §6): raycasts and solid-box queries.
//
// Golden raycast tables: rays from integer hashing (exact float inputs) through hashed worlds.
// The legacy and cube-registry tables were recorded on the pre-V3 raycast (commit 27e7788) and
// must stay bit-identical (cell, normal, distance bits, value): cube cells keep the original
// traversal exactly. The shape-world tables (V3) hash only discrete fields; their distances are
// checked against a marching reference with tolerances.
#include <doctest/doctest.h>

#include "Core/Error.h"
#include "Core/JobSystem.h"
#include "Scene/Voxel/VoxelWorld.h"
#include "../Support/MeshHash.h"
#include "../Support/VoxelTestWorlds.h"

#include <bit>
#include <chrono>
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

namespace {

// Test reference: first point along the ray (steps of `h` voxel units) that lies inside a solid
// (cube cell, Shape::contains, or for Collision the collision boxes of colliding cells).
bool solidAt(const VoxelWorld& w, const glm::vec3& voxelPoint, RaycastMode mode) {
    const glm::ivec3 cell(glm::floor(voxelPoint));
    const Voxel v = w.getVoxel(cell);
    if (v == kEmpty) return false;
    const BlockTable& table = w.blocks();
    if (mode == RaycastMode::Collision && !table.collides(v)) return false;
    if (table.isCube(v)) return true;
    const Shape& s = *table.shape(v);
    const glm::vec3 l = voxelPoint - glm::vec3(cell);
    if (mode == RaycastMode::Selection) return s.contains(l);
    for (const ShapeBox& b : s.collision) {
        if (glm::all(glm::greaterThanEqual(l, b.min)) && glm::all(glm::lessThanEqual(l, b.max))) return true;
    }
    return false;
}

std::optional<float> marchFirstSolid(const VoxelWorld& w, const Ray& r, RaycastMode mode, float h) {
    const float vs = w.settings().voxelSize;
    const glm::vec3 o = r.origin / vs;
    const glm::vec3 d = glm::normalize(r.direction);
    const float maxT = r.maxDistance / vs;
    for (float t = 0.0f; t <= maxT; t += h) {
        if (solidAt(w, o + d * t, mode)) return t * vs;
    }
    return std::nullopt;
}

// Discrete fields only (cell, normal, value): distances of polygon hits are compared against
// the reference with tolerances instead, so the table holds across compilers.
uint64_t discreteHash(const VoxelWorld& world, const std::vector<Ray>& rays, RaycastMode mode, int* hits) {
    Fnv h;
    *hits = 0;
    for (const Ray& r : rays) {
        const auto hit = world.raycast(r.origin, r.direction, r.maxDistance, {mode});
        h.value(uint8_t(hit ? 1 : 0));
        if (!hit) continue;
        ++*hits;
        h.value(hit->voxel);
        h.value(hit->normal);
        h.value(hit->value);
    }
    return h.get();
}

// Some solid point within `eps` voxel units of `p` (27 probes at three scales): hits where the
// ray only touches a solid (an edge or corner of a cell, as the cube traversal always did, or
// the thin rim where an arc meets a flat face) count as on it.
bool nearSolid(const VoxelWorld& w, const glm::vec3& voxelPoint, RaycastMode mode, float eps) {
    for (float e : {eps, eps * 0.1f, eps * 0.01f}) {
        for (int k = 0; k < 27; ++k) {
            const glm::vec3 off(float(k % 3 - 1), float(k / 3 % 3 - 1), float(k / 9 - 1));
            if (solidAt(w, voxelPoint + off * e, mode)) return true;
        }
    }
    return false;
}

// The hit point lies on the closed surface of the hit cell's solid (a polygon of its shape, or
// the cell's boundary for cubes): accepts touches of zero-thickness rims (an arc meeting a face).
bool onHitSurface(const VoxelWorld& w, const RaycastHit& hit, RaycastMode mode) {
    const glm::vec3 l = hit.point / w.settings().voxelSize - glm::vec3(hit.voxel);
    constexpr float e = 1e-5f;
    if (glm::any(glm::lessThan(l, glm::vec3(-e))) || glm::any(glm::greaterThan(l, glm::vec3(1.0f + e)))) return false;
    const Shape* shape = w.blocks().shape(hit.value);
    if (!shape) return true; // cube: any point of the closed cell box is on or in it
    if (mode == RaycastMode::Collision) {
        for (const ShapeBox& b : shape->collision) {
            if (glm::all(glm::greaterThanEqual(l, b.min - e)) && glm::all(glm::lessThanEqual(l, b.max + e))) return true;
        }
        return false;
    }
    for (const Shape::Polygon& poly : shape->polygons) {
        const glm::vec3& a = poly.corners[0];
        if (std::abs(glm::dot(l - a, poly.normal)) > e) continue;
        for (size_t k = 1; k + 1 < poly.corners.size(); ++k) {
            const glm::vec3 b = poly.corners[k];
            const glm::vec3 c = poly.corners[k + 1];
            const float area = glm::dot(glm::cross(b - a, c - a), poly.normal);
            const float u = glm::dot(glm::cross(c - b, l - b), poly.normal) / area;
            const float v = glm::dot(glm::cross(a - c, l - c), poly.normal) / area;
            if (u >= -e && v >= -e && u + v <= 1.0f + e) return true;
        }
    }
    return false;
}

// Checks every ray against the marching reference. Returns the rays whose hit is neither
// confirmed by the reference nor on (within 1e-3 voxel of) a solid.
int checkAgainstReference(const VoxelWorld& w, const std::vector<Ray>& rays, RaycastMode mode) {
    constexpr float kStep = 1.0f / 32.0f; // voxel units
    const float vs = w.settings().voxelSize;
    int bad = 0;
    for (size_t i = 0; i < rays.size(); ++i) {
        const Ray& r = rays[i];
        CAPTURE(i);
        const auto hit = w.raycast(r.origin, r.direction, r.maxDistance, {mode});
        auto ref = marchFirstSolid(w, r, mode, kStep);
        if (ref && *ref == 0.0f) {
            // Origin on the solid's closed surface: a ray leaving it is not a hit (cube +X/+Y/+Z
            // faces and Collision-mode box faces); march again from just past the origin.
            const glm::vec3 d = glm::normalize(r.direction);
            const glm::vec3 o = r.origin / vs;
            if (!solidAt(w, o + d * 1e-3f, mode) && !solidAt(w, o + d * 2e-3f, mode)) {
                Ray past = r;
                past.origin = r.origin + d * (2e-3f * vs);
                past.maxDistance = r.maxDistance - 2e-3f * vs;
                ref = marchFirstSolid(w, past, mode, kStep);
                if (ref) *ref += 2e-3f * vs;
            }
        }
        if (ref) {
            // Never leaks through solid the reference found.
            REQUIRE(hit.has_value());
            CHECK(hit->distance <= *ref + 1e-3f * vs);
        }
        if (!hit) continue;
        // An earlier hit than the reference (or one it stepped over) must touch a solid.
        if ((!ref || hit->distance < *ref - (kStep + 1e-3f) * vs) && !nearSolid(w, hit->point / vs, mode, 1e-3f) &&
            !onHitSurface(w, *hit, mode)) {
            ++bad;
        }
        CHECK(hit->distance <= r.maxDistance);
        const glm::vec3 d = glm::normalize(r.direction);
        CHECK(glm::length(hit->point - (r.origin + d * hit->distance)) <= 1e-4f * (1.0f + hit->distance));
        if (hit->distance > 0.0f || hit->normal != glm::ivec3(0)) {
            CHECK(glm::dot(hit->surfaceNormal, d) <= 0.0f); // front face (cube faces: entered face)
            CHECK(std::abs(glm::length(hit->surfaceNormal) - 1.0f) < 1e-5f);
        } else {
            CHECK(hit->surfaceNormal == glm::vec3(0.0f));
        }
    }
    return bad;
}

} // namespace

TEST_CASE("voxel raycast: shape worlds hit the exact geometry (Selection) and the boxes (Collision)") {
    const ShapeRegistry reg;
    const auto gen = std::make_shared<QueryTerrain>(7, QueryTerrain::Mode::Shapes, reg.shapeCount(), reg.noCollisionOffset);
    VoxelWorld world(gen, reg.palette, test::queryWorldSettings(1.0f));
    test::settleWorld(world, {8, 8, 8});
    const auto rays = testRays(1.0f);
    std::vector<Ray> sample;
    for (size_t i = 0; i < rays.size(); i += 3) sample.push_back(rays[i]);

    int hits = 0;
    const uint64_t selection = discreteHash(world, rays, RaycastMode::Selection, &hits);
    MESSAGE("selection hits ", hits, " hash ", test::hex64(selection));
    CHECK(selection == 0x53599CECA851519Cull);
    const uint64_t collision = discreteHash(world, rays, RaycastMode::Collision, &hits);
    MESSAGE("collision hits ", hits, " hash ", test::hex64(collision));
    // Re-baselined for the audit I1 fix (origins on outer box faces: face normal, not inside);
    // the previous value was 0x3490C3C2CAD63E7D. Selection is unchanged.
    CHECK(collision == 0x7BB0DF2FA3F56246ull);

    CHECK(checkAgainstReference(world, sample, RaycastMode::Selection) == 0);
    CHECK(checkAgainstReference(world, sample, RaycastMode::Collision) == 0);

    // Worker threads: same results.
    JobSystem jobs(3);
    VoxelWorld threaded(gen, reg.palette, test::queryWorldSettings(1.0f), &jobs);
    test::settleWorld(threaded, {8, 8, 8});
    CHECK(discreteHash(threaded, rays, RaycastMode::Selection, &hits) == selection);
    CHECK(discreteHash(threaded, rays, RaycastMode::Collision, &hits) == collision);
}

TEST_CASE("voxel raycast: legacy worlds give identical results in both modes") {
    VoxelWorld world(std::make_shared<QueryTerrain>(5, QueryTerrain::Mode::Legacy), test::legacyTestPalette(),
                     test::queryWorldSettings(1.0f));
    test::settleWorld(world, {8, 8, 8});
    for (const Ray& r : testRays(1.0f)) {
        const auto a = world.raycast(r.origin, r.direction, r.maxDistance);
        const auto b = world.raycast(r.origin, r.direction, r.maxDistance, {RaycastMode::Collision});
        REQUIRE(a.has_value() == b.has_value());
        if (!a) continue;
        CHECK(a->voxel == b->voxel);
        CHECK(a->normal == b->normal);
        CHECK(std::bit_cast<uint32_t>(a->distance) == std::bit_cast<uint32_t>(b->distance));
        CHECK(a->surfaceNormal == glm::vec3(a->normal));
    }
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

namespace {

// One shape block per (shape, rotation) on a sparse grid at 1 m cells, plus the no-collision
// variants; a floor far below so downward misses stay misses within the ray length.
struct ShapeGallery {
    ShapeRegistry reg;
    PlacedWorld pw;
    static glm::ivec3 cellOf(uint16_t shape, int rotation) { return {int(shape) * 3 - 12, 4, rotation * 3 - 6}; }
    ShapeGallery() {
        for (uint16_t s = 0; s < reg.shapeCount(); ++s) {
            for (int r = 0; r < 4; ++r) {
                pw.gen->cells.push_back({cellOf(s, r), ShapeRegistry::id(s, r)});
                pw.gen->cells.push_back({cellOf(s, r) + glm::ivec3(0, 0, 20),
                                         Voxel(ShapeRegistry::id(s, r) + reg.noCollisionOffset)});
            }
        }
        pw.build(reg.palette, 1.0f);
    }
    const VoxelWorld& world() const { return *pw.world; }
};

// First solid point inside one cell along a ray (fine march, cell-local), for exact checks.
std::optional<float> firstInside(const Shape& s, const glm::vec3& o, const glm::vec3& d, float tMax) {
    for (int i = 0; i <= 8192; ++i) {
        const float t = tMax * float(i) / 8192.0f;
        if (s.contains(o + d * t)) return t;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("voxel raycast: every shape and rotation, axis and diagonal rays") {
    const ShapeGallery g;
    const VoxelWorld& w = g.world();
    const glm::vec3 dirs[] = {{0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1},
                              {0.6f, -0.8f, 0}, {0, -0.8f, 0.6f}, {-0.48f, -0.6f, 0.64f}};
    int checked = 0;
    for (uint16_t s = 1; s < g.reg.shapeCount(); ++s) {
        for (int r = 0; r < 4; ++r) {
            const Shape shape = rotateShape((*g.reg.lib)[s], r);
            const glm::vec3 cell(ShapeGallery::cellOf(s, r));
            for (const glm::vec3& d : dirs) {
                for (int k = 0; k < 16; ++k) {
                    // Entry points on the cell face opposite to the ray, at sample offsets.
                    const glm::vec2 uv((float(k % 4) + 0.37f) / 4.0f, (float(k / 4) + 0.61f) / 4.0f);
                    glm::vec3 local(0.5f);
                    const int axis = std::abs(d.y) >= std::abs(d.x) && std::abs(d.y) >= std::abs(d.z) ? 1
                                     : std::abs(d.x) >= std::abs(d.z)                            ? 0
                                                                                                 : 2;
                    local[axis] = d[axis] > 0.0f ? 0.0f : 1.0f;
                    local[(axis + 1) % 3] = uv.x;
                    local[(axis + 2) % 3] = uv.y;
                    const glm::vec3 start = cell + local - d * 0.25f; // just outside the cell
                    const auto hit = w.raycast(start, d, 3.0f);
                    // Reference inside this cell (cell-local, fine march over the cell's span).
                    const auto ref = firstInside(shape, local, d, 1.8f);
                    CAPTURE(shape.name);
                    CAPTURE(r);
                    CAPTURE(k);
                    if (!ref) {
                        // The ray misses this cell's solid; it may only hit nothing within 3 m.
                        CHECK_FALSE((hit && hit->voxel == glm::ivec3(cell)));
                        continue;
                    }
                    REQUIRE(hit.has_value());
                    CHECK(hit->voxel == glm::ivec3(cell));
                    CHECK(hit->value == ShapeRegistry::id(s, r));
                    // Exact polygon hit vs reference march (step 1.8 / 8192 = 2.2e-4).
                    CHECK(hit->distance - 0.25f <= *ref + 1e-5f);
                    CHECK(hit->distance - 0.25f >= *ref - 3e-4f);
                    CHECK(glm::dot(hit->surfaceNormal, d) < 0.0f);
                    CHECK(hit->normal == [&] {
                        const glm::vec3 a = glm::abs(hit->surfaceNormal);
                        const int ax = (a.y >= a.x && a.y >= a.z) ? 1 : a.x >= a.z ? 0 : 2;
                        glm::ivec3 n(0);
                        n[ax] = hit->surfaceNormal[ax] > 0.0f ? 1 : -1;
                        return n;
                    }());
                    ++checked;
                }
            }
        }
    }
    CHECK(checked > 2000);
}

TEST_CASE("voxel raycast: ray origins inside shape solids and in their empty parts") {
    const ShapeGallery g;
    const VoxelWorld& w = g.world();
    const auto at = [&](const char* name, int r, glm::vec3 local) {
        return glm::vec3(ShapeGallery::cellOf(*g.reg.lib->find(name), r)) + local;
    };
    // Inside the solid: hit at distance 0, normal (0, 0, 0), in every direction.
    const struct {
        const char* shape;
        glm::vec3 local;
    } inside[] = {{"wedge", {0.5f, 0.2f, 0.6f}},        {"stair", {0.5f, 0.75f, 0.75f}},
                  {"corner_inner", {0.9f, 0.8f, 0.1f}}, {"corner_outer", {0.9f, 0.5f, 0.6f}},
                  {"quarter_round", {0.5f, 0.6f, 0.6f}}, {"quarter_round_inner", {0.5f, 0.8f, 0.8f}},
                  {"panel_thin", {0.5f, 0.5f, 0.0625f}}, {"slab_low", {0.1f, 0.1f, 0.9f}}};
    for (const auto& c : inside) {
        CAPTURE(c.shape);
        for (const glm::vec3 d : {glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(1, 0.3f, -0.2f)}) {
            const auto hit = w.raycast(at(c.shape, 0, c.local), d, 5.0f);
            REQUIRE(hit.has_value());
            CHECK(hit->distance == 0.0f);
            CHECK(hit->normal == glm::ivec3(0));
            CHECK(hit->surfaceNormal == glm::vec3(0.0f));
            CHECK(hit->point == at(c.shape, 0, c.local));
        }
    }
    // Empty part of a wedge (above the slope y = z): +Z hits the slope inside the same cell.
    auto hit = w.raycast(at("wedge", 0, {0.5f, 0.8f, 0.3f}), {0, 0, 1}, 5.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->voxel == ShapeGallery::cellOf(*g.reg.lib->find("wedge"), 0));
    CHECK(hit->distance == doctest::Approx(0.5f).epsilon(1e-5));
    CHECK(hit->normal == glm::ivec3(0, 1, 0)); // slope normal (0, .707, -.707): tie prefers Y
    CHECK(hit->surfaceNormal.y == doctest::Approx(0.70710678f));
    CHECK(hit->surfaceNormal.z == doctest::Approx(-0.70710678f));
    // Empty part, pointing away from the solid: the ray leaves the cell and hits nothing.
    CHECK_FALSE(w.raycast(at("wedge", 0, {0.5f, 0.8f, 0.3f}), {0, 1, 0}, 5.0f));
    // Stair notch (empty lower front quarter above the lower tread): +Z hits the upper riser.
    hit = w.raycast(at("stair", 0, {0.5f, 0.75f, 0.1f}), {0, 0, 1}, 5.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->distance == doctest::Approx(0.4f).epsilon(1e-5));
    CHECK(hit->normal == glm::ivec3(0, 0, -1));
    // Inside the concave arch's empty quarter circle: hits the arc from inside the cell.
    hit = w.raycast(at("quarter_round_inner", 0, {0.5f, 0.3f, 0.3f}), {0, 0.6f, 0.8f}, 5.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->distance > 0.4f);
    CHECK(hit->distance < 0.6f);
    CHECK(glm::dot(hit->surfaceNormal, glm::vec3(0, 0.6f, 0.8f)) < 0.0f);
    // Collision mode: origin inside a collision box is distance 0; in a box gap it continues.
    hit = w.raycast(at("wedge", 0, {0.5f, 0.1f, 0.05f}), {0, 1, 0}, 5.0f, {RaycastMode::Collision});
    REQUIRE(hit.has_value()); // lowest step box reaches y = 1/8
    CHECK(hit->distance == 0.0f);
    CHECK(hit->normal == glm::ivec3(0));
    CHECK_FALSE(w.raycast(at("wedge", 0, {0.5f, 0.2f, 0.05f}), {0, 1, 0}, 5.0f, {RaycastMode::Collision}));
}

TEST_CASE("voxel raycast: exact boundaries, shared edges and near-parallel rays") {
    const ShapeRegistry reg;
    PlacedWorld pw;
    // Row of 120 slab_low cells along +Z at y = 0, x = 0; two wedges side by side at x = 5, 6.
    for (int z = 0; z < 120; ++z) pw.gen->cells.push_back({{0, 0, z}, reg.id("slab_low")});
    pw.gen->cells.push_back({{5, 0, 3}, reg.id("wedge")});
    pw.gen->cells.push_back({{6, 0, 3}, reg.id("wedge")});
    pw.build(reg.palette, 1.0f);
    const VoxelWorld& w = *pw.world;

    // Ray in the plane x = 0 (a cell boundary) at slab height enters the first slab's -Z face.
    auto hit = w.raycast({0.0f, 0.25f, -2.0f}, {0, 0, 1}, 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->voxel == glm::ivec3(0, 0, 0));
    CHECK(hit->distance == 2.0f);
    CHECK(hit->normal == glm::ivec3(0, 0, -1));
    // Exactly at the top edge of the -Z face (y = 0.5): closed edges count, so it hits.
    hit = w.raycast({0.5f, 0.5f, -2.0f}, {0, 0, 1}, 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->distance == 2.0f);
    // Slightly above: runs along the treads without hitting (grazing faces are ignored).
    CHECK_FALSE(w.raycast({0.5f, 0.5001f, -2.0f}, {0, 0, 1}, 150.0f));
    // Origin exactly on the tread, ray in the tread plane: the tread is grazing (ignored); the
    // next cell's -Z face has its top edge in that plane, and closed edges count, so the ray
    // stops at the cell boundary (documented: a ray lying exactly in a face plane stops at the
    // edge of the next touching face; shape faces between cells are not culled for queries).
    hit = w.raycast({0.5f, 0.5f, 0.5f}, {0, 0, 1}, 150.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->voxel == glm::ivec3(0, 0, 1));
    CHECK(hit->distance == 0.5f);
    CHECK(hit->normal == glm::ivec3(0, 0, -1));
    // Near-parallel: descending 1e-3 per metre from 0.6005 reaches the tread (0.5) at z = 100.5.
    hit = w.raycast({0.5f, 0.6005f, 0.0f}, {0.0f, -1e-3f, 1.0f}, 200.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->voxel == glm::ivec3(0, 0, 100));
    CHECK(hit->normal == glm::ivec3(0, 1, 0));
    CHECK(hit->distance == doctest::Approx(100.50005f).epsilon(1e-5));
    // Almost exactly parallel (1e-9 slope): never reaches the tread within 150 m.
    CHECK_FALSE(w.raycast({0.5f, 0.6f, 0.0f}, {0.0f, -1e-9f, 1.0f}, 150.0f));

    // Shared edge between two wedges (x = 6): a vertical ray exactly on it hits the slope.
    hit = w.raycast({6.0f, 3.0f, 3.5f}, {0, -1, 0}, 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->distance == doctest::Approx(2.5f).epsilon(1e-6));
    CHECK(hit->normal == glm::ivec3(0, 1, 0));
    // The fan diagonal of the slope quad (corner 0 to corner 2) never leaks either.
    for (int k = 0; k < 16; ++k) {
        const float f = float(k) / 16.0f;
        hit = w.raycast({5.0f + f, 3.0f, 3.0f + f}, {0, -1, 0}, 10.0f);
        CAPTURE(k);
        REQUIRE(hit.has_value());
        CHECK(hit->distance == doctest::Approx(3.0f - f).epsilon(1e-6));
    }
    // At f = 1 the ray lies on the planes x = 6 and z = 4: like the cube traversal it walks the
    // cells (6, y, 4) (floor rule), which are empty, so it misses the wedges' shared corner edge.
    CHECK_FALSE(w.raycast({6.0f, 3.0f, 4.0f}, {0, -1, 0}, 10.0f));
}

TEST_CASE("voxel raycast: collision flags and collision mode") {
    const ShapeGallery g;
    const VoxelWorld& w = g.world();
    const uint16_t wedge = *g.reg.lib->find("wedge");
    const glm::vec3 solid(ShapeGallery::cellOf(wedge, 0));
    const glm::vec3 ghost = solid + glm::vec3(0, 0, 20); // same wedge with kNoCollision
    // Selection hits no-collision blocks (grates stay selectable); Collision skips them.
    auto hit = w.raycast(ghost + glm::vec3(0.5f, 2.0f, 0.6f), {0, -1, 0}, 3.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->value == ShapeRegistry::id(wedge, 0) + g.reg.noCollisionOffset);
    CHECK_FALSE(w.raycast(ghost + glm::vec3(0.5f, 2.0f, 0.6f), {0, -1, 0}, 3.0f, {RaycastMode::Collision}));
    // Collision hits the stepped box (level 2 of 4: top at 5/8) instead of the slope (0.6).
    hit = w.raycast(solid + glm::vec3(0.5f, 2.0f, 0.6f), {0, -1, 0}, 3.0f, {RaycastMode::Collision});
    REQUIRE(hit.has_value());
    CHECK(hit->distance == 2.0f - 0.625f);
    CHECK(hit->normal == glm::ivec3(0, 1, 0));
    CHECK(hit->surfaceNormal == glm::vec3(0, 1, 0));
    hit = w.raycast(solid + glm::vec3(0.5f, 2.0f, 0.6f), {0, -1, 0}, 3.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->distance == doctest::Approx(1.4f).epsilon(1e-6));
    // Collision-mode hit points lie on a box from collectSolidBoxes.
    for (uint16_t s = 1; s < g.reg.shapeCount(); ++s) {
        for (int r = 0; r < 4; ++r) {
            const glm::vec3 cell(ShapeGallery::cellOf(s, r));
            for (int k = 0; k < 9; ++k) {
                const glm::vec3 o = cell + glm::vec3((k % 3 + 0.5f) / 3.0f, 2.0f, (k / 3 + 0.5f) / 3.0f);
                hit = w.raycast(o, {0.1f, -1, 0.05f}, 4.0f, {RaycastMode::Collision});
                if (!hit) continue;
                std::vector<SolidBox> boxes;
                REQUIRE(w.collectSolidBoxes({hit->point, hit->point}, boxes) == QueryStatus::Complete);
                bool on = false;
                for (const SolidBox& b : boxes) {
                    on = on || (glm::all(glm::greaterThanEqual(hit->point, b.min - 1e-5f)) &&
                                glm::all(glm::lessThanEqual(hit->point, b.max + 1e-5f)));
                }
                CHECK(on);
            }
        }
    }
}

// Informational timings (skipped by default; never a CI failure). Run in Release:
//   ZodarTests --no-skip -tc='voxel query timing*'
// Targets from the V3 plan (not asserted): player box query < 5 us, 64 m ray < 20 us.
TEST_CASE("voxel query timing: player box queries and 64 m rays" * doctest::skip()) {
    const ShapeRegistry reg;
    const auto gen = std::make_shared<QueryTerrain>(7, QueryTerrain::Mode::Shapes, reg.shapeCount(), reg.noCollisionOffset);
    VoxelWorld world(gen, reg.palette, test::queryWorldSettings(0.5f));
    test::settleWorld(world, {4, 4, 4});
    const auto rays = testRays(0.5f);
    std::vector<SolidBox> out;
    size_t guard = 0;
    double bestBox = 1e30;
    double bestSel = 1e30;
    double bestCol = 1e30;
    for (int rep = 0; rep < 7; ++rep) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 4096; ++i) {
            const uint64_t r = hash3(i, 9, 9, 1);
            const glm::vec3 feet(float(r % 64) * 0.25f - 8.0f, float((r >> 8) % 24) * 0.25f, float((r >> 16) % 64) * 0.25f - 8.0f);
            out.clear();
            (void)world.collectSolidBoxes({feet - glm::vec3(0.3f, 0.0f, 0.3f), feet + glm::vec3(0.3f, 1.8f, 0.3f)}, out);
            guard += out.size();
        }
        bestBox = std::min(bestBox, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 4096.0);
        for (RaycastMode mode : {RaycastMode::Selection, RaycastMode::Collision}) {
            t0 = std::chrono::steady_clock::now();
            for (const Ray& r : rays) {
                guard += world.raycast(r.origin, r.direction, 64.0f, {mode}) ? 1 : 0;
            }
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() /
                              static_cast<double>(rays.size());
            (mode == RaycastMode::Selection ? bestSel : bestCol) = std::min(mode == RaycastMode::Selection ? bestSel : bestCol, us);
        }
    }
    MESSAGE("player box query ", bestBox, " us (target < 5), ray Selection ", bestSel, " us, Collision ", bestCol,
            " us (target < 20, rays up to 64 m)");
    CHECK(guard > 0);
}

TEST_CASE("voxel raycast: Collision-mode origins on collision-box faces behave like cube faces") {
    // Audit I1: an origin exactly on the outer face of a collision box must give an entry hit at
    // distance 0 with that face's normal (inward ray) or leave the solid (outward ray), as a
    // cube's +X/+Y/+Z face does; only faces shared by two boxes of the union count as inside.
    // (Cube cells keep their pre-V3 floor rule, checked at the end: an origin on a cube's
    // -X/-Y/-Z face lies in the cube's cell and counts as inside.)
    const ShapeGallery g;
    const VoxelWorld& w = g.world();
    const BlockTable& table = w.blocks();
    const auto inUnion = [](const Shape& s, const glm::vec3& p) {
        for (const ShapeBox& b : s.collision) {
            if (glm::all(glm::greaterThanEqual(p, b.min)) && glm::all(glm::lessThanEqual(p, b.max))) return true;
        }
        return false;
    };
    int inward = 0;
    int outward = 0;
    int inexact = 0;
    for (uint16_t s = 1; s < g.reg.shapeCount(); ++s) {
        for (int r = 0; r < 4; ++r) {
            const Voxel id = ShapeRegistry::id(s, r);
            const glm::vec3 cell(ShapeGallery::cellOf(s, r));
            const Shape* shape = table.shape(id);
            const std::vector<ShapeBox> boxes =
                shape ? shape->collision : std::vector<ShapeBox>{{glm::vec3(0.0f), glm::vec3(1.0f)}};
            const Shape cubeShape = (*g.reg.lib)[ShapeLibrary::kCube];
            const Shape& union_ = shape ? *shape : cubeShape;
            for (size_t bi = 0; bi < boxes.size(); ++bi) {
                const ShapeBox& b = boxes[bi];
                for (int f = 0; f < 6; ++f) { // -X, +X, -Y, +Y (bottom, top), -Z, +Z
                    const int axis = f >> 1;
                    glm::vec3 n(0.0f);
                    n[axis] = (f & 1) ? 1.0f : -1.0f;
                    // Face centre, nudged off the centre so it never lies on another box's edge.
                    glm::vec3 p = (b.min + b.max) * 0.5f;
                    p[axis] = (f & 1) ? b.max[axis] : b.min[axis];
                    p[(axis + 1) % 3] = b.min[(axis + 1) % 3] + (b.max[(axis + 1) % 3] - b.min[(axis + 1) % 3]) * 0.4375f;
                    p[(axis + 2) % 3] = b.min[(axis + 2) % 3] + (b.max[(axis + 2) % 3] - b.min[(axis + 2) % 3]) * 0.5625f;
                    const bool outer = !inUnion(union_, p + n * 1e-3f) && inUnion(union_, p - n * 1e-3f);
                    CAPTURE(union_.name);
                    CAPTURE(r);
                    CAPTURE(bi);
                    CAPTURE(f);
                    const glm::vec3 origin = cell + p;
                    if (origin[axis] - cell[axis] != p[axis]) {
                        ++inexact; // the face is not representable at this world position
                        continue;
                    }
                    if (!outer) {
                        // Shared with another box: the origin is inside the solid.
                        if (inUnion(union_, p + n * 1e-3f) && inUnion(union_, p - n * 1e-3f)) {
                            const auto hit = w.raycast(origin, n, 1.5f, {RaycastMode::Collision});
                            REQUIRE(hit.has_value());
                            CHECK(hit->distance == 0.0f);
                            CHECK(hit->normal == glm::ivec3(0));
                        }
                        continue;
                    }
                    // Inward: entry hit at distance 0 with the face normal.
                    auto hit = w.raycast(origin, -n, 1.5f, {RaycastMode::Collision});
                    REQUIRE(hit.has_value());
                    CHECK(hit->voxel == glm::ivec3(cell));
                    CHECK(hit->distance == 0.0f);
                    CHECK(hit->normal == glm::ivec3(n));
                    CHECK(hit->surfaceNormal == n);
                    CHECK(hit->point == origin);
                    ++inward;
                    // Inward but oblique: same face and normal.
                    glm::vec3 oblique = -n;
                    oblique[(axis + 1) % 3] = 0.3f;
                    hit = w.raycast(origin, oblique, 1.5f, {RaycastMode::Collision});
                    REQUIRE(hit.has_value());
                    CHECK(hit->distance == 0.0f);
                    CHECK(hit->normal == glm::ivec3(n));
                    // Outward: leaves this cell's solid (gallery cells are 3 apart, so nothing else
                    // lies within 1.5 m).
                    hit = w.raycast(origin, n, 1.5f, {RaycastMode::Collision});
                    CHECK_FALSE(hit.has_value());
                    ++outward;
                }
            }
        }
    }
    MESSAGE("box faces: inward ", inward, " outward ", outward, " not representable ", inexact);
    CHECK(inward > 200);
    CHECK(outward == inward);
    // The audit case: feet resting on a slab top and on a cube top, probing down.
    const glm::vec3 slabTop = glm::vec3(ShapeGallery::cellOf(*g.reg.lib->find("slab_low"), 0)) + glm::vec3(0.5f, 0.5f, 0.5f);
    const glm::vec3 cubeTop = glm::vec3(ShapeGallery::cellOf(ShapeLibrary::kCube, 0)) + glm::vec3(0.5f, 1.0f, 0.5f);
    for (RaycastMode mode : {RaycastMode::Selection, RaycastMode::Collision}) {
        for (const glm::vec3& o : {slabTop, cubeTop}) {
            const auto hit = w.raycast(o, {0, -1, 0}, 2.0f, {mode});
            REQUIRE(hit.has_value());
            CHECK(hit->distance == 0.0f);
            CHECK(hit->normal == glm::ivec3(0, 1, 0));
        }
    }
    // Cube reference (pre-V3, bit-exact): +X/+Y/+Z faces give entry hits with the face normal
    // and outward rays leave; on -X/-Y/-Z faces the origin is in the cube's cell (floor rule),
    // so every direction hits at distance 0 with normal (0, 0, 0).
    const glm::vec3 cube(ShapeGallery::cellOf(ShapeLibrary::kCube, 0));
    for (int f = 0; f < 6; ++f) {
        const int axis = f >> 1;
        glm::vec3 n(0.0f);
        n[axis] = (f & 1) ? 1.0f : -1.0f;
        glm::vec3 p(0.4375f, 0.5625f, 0.5f);
        p[axis] = (f & 1) ? 1.0f : 0.0f;
        CAPTURE(f);
        for (RaycastMode mode : {RaycastMode::Selection, RaycastMode::Collision}) {
            const auto in = w.raycast(cube + p, -n, 1.5f, {mode});
            const auto out = w.raycast(cube + p, n, 1.5f, {mode});
            REQUIRE(in.has_value());
            CHECK(in->distance == 0.0f);
            if (f & 1) {
                CHECK(in->normal == glm::ivec3(n));
                CHECK_FALSE(out.has_value());
            } else {
                CHECK(in->normal == glm::ivec3(0));
                REQUIRE(out.has_value());
                CHECK(out->normal == glm::ivec3(0));
            }
        }
    }
    // A shared face inside the union (wedge levels 0 and 1 meet at z = 1/4 below y = 1/8).
    const glm::vec3 shared = glm::vec3(ShapeGallery::cellOf(*g.reg.lib->find("wedge"), 0)) + glm::vec3(0.5f, 0.0625f, 0.25f);
    for (const glm::vec3 d : {glm::vec3(0, 0, 1), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)}) {
        const auto hit = w.raycast(shared, d, 2.0f, {RaycastMode::Collision});
        REQUIRE(hit.has_value());
        CHECK(hit->distance == 0.0f);
        CHECK(hit->normal == glm::ivec3(0));
    }
}

TEST_CASE("voxel raycast: invalid inputs are rejected") {
    const ShapeGallery g;
    const VoxelWorld& w = g.world();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS((void)w.raycast({nan, 0, 0}, {0, -1, 0}, 5.0f), zodar::Error);
    CHECK_THROWS_AS((void)w.raycast({0, inf, 0}, {0, -1, 0}, 5.0f), zodar::Error);
    CHECK_THROWS_AS((void)w.raycast({0, 0, 0}, {0, nan, 0}, 5.0f), zodar::Error);
    CHECK_THROWS_AS((void)w.raycast({0, 0, 0}, {-inf, 0, 0}, 5.0f), zodar::Error);
    CHECK_THROWS_AS((void)w.raycast({0, 0, 0}, {0, -1, 0}, nan), zodar::Error);
    CHECK_THROWS_AS((void)w.raycast({0, 0, 0}, {0, -1, 0}, inf), zodar::Error);
    CHECK_THROWS_AS((void)w.raycast({0, 0, 0}, {0, -1, 0}, -1.0f), zodar::Error);
    CHECK_THROWS_AS((void)w.raycast({2e9f, 0, 0}, {0, -1, 0}, 5.0f), zodar::Error);
    CHECK_THROWS_AS((void)w.raycast({0, 0, 0}, {0, -1, 0}, 2e9f), zodar::Error);
    // Unchanged: zero direction or zero distance give no hit.
    CHECK_FALSE(w.raycast({0, 10, 0}, {0, 0, 0}, 5.0f));
    CHECK_FALSE(w.raycast({0, 10, 0}, {0, -1, 0}, 0.0f));
}
