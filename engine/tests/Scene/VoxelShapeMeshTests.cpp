#include <doctest/doctest.h>

#include "Core/JobSystem.h"
#include "Scene/Voxel/ChunkMesher.h"
#include "Scene/Voxel/VoxelWorld.h"

#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>

using namespace zodar;
using namespace zodar::voxel;

namespace {

// Registry palette: id 1 = cube (material 1); id 2 + shape * 4 + rotation = every built-in shape
// at every rotation (material 2). Material 3 = second cube material (id 3 is a shape, so the
// second cube gets the last id).
struct Registry {
    std::shared_ptr<ShapeLibrary> lib = std::make_shared<ShapeLibrary>(ShapeLibrary::builtins());
    VoxelPalette palette;
    Voxel cube = 1;
    Voxel cube2 = 0;

    Registry() {
        palette.colors = {glm::vec4(0), glm::vec4(0.5f, 0.5f, 0.5f, 1), glm::vec4(0.8f, 0.6f, 0.4f, 1),
                          glm::vec4(0.1f, 0.2f, 0.3f, 1)};
        palette.faceLayers = {{0, 0, 0}, {1, 2, 3}, {4, 5, 6}, {7, 7, 7}};
        palette.shapes = lib;
        palette.blocks.resize(2 + lib->size() * 4 + 1);
        palette.blocks[1] = {1, ShapeLibrary::kCube, 0, 0};
        for (uint16_t s = 0; s < lib->size(); ++s) {
            for (uint8_t r = 0; r < 4; ++r) palette.blocks[id(s, r)] = {2, s, r, 0};
        }
        cube2 = static_cast<Voxel>(palette.blocks.size() - 1);
        palette.blocks[cube2] = {3, ShapeLibrary::kCube, 0, 0};
    }
    static Voxel id(uint16_t shape, int rotation) { return static_cast<Voxel>(2 + shape * 4 + rotation); }
    Voxel id(const char* name, int rotation = 0) const { return id(*lib->find(name), rotation); }
};

ChunkNeighborhood single(const VoxelChunk& c) {
    ChunkNeighborhood nb;
    nb.chunks[13] = &c;
    return nb;
}

bool sameBytes(const MeshData& a, const MeshData& b) {
    return a.vertices.size() == b.vertices.size() && a.indices == b.indices &&
           (a.vertices.empty() ||
            std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(Vertex)) == 0);
}

// Signed volume enclosed by the triangles (positive when they face outward).
double meshVolume(const MeshData& m) {
    double v = 0.0;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::dvec3 a = m.vertices[m.indices[i]].position;
        const glm::dvec3 b = m.vertices[m.indices[i + 1]].position;
        const glm::dvec3 c = m.vertices[m.indices[i + 2]].position;
        v += glm::dot(a, glm::cross(b, c));
    }
    return v / 6.0;
}

size_t countFaces(const MeshData& m, const glm::vec3& normal, int axis, float plane) {
    size_t n = 0;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const Vertex& a = m.vertices[m.indices[i]];
        const Vertex& b = m.vertices[m.indices[i + 1]];
        const Vertex& c = m.vertices[m.indices[i + 2]];
        if (glm::all(glm::equal(a.normal, normal)) && a.position[axis] == plane && b.position[axis] == plane &&
            c.position[axis] == plane) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("shape mesher: isolated shape cells emit closed, outward geometry at every rotation") {
    const Registry reg;
    const BlockTable table(reg.palette);
    for (uint16_t s = 1; s < reg.lib->size(); ++s) {
        for (int r = 0; r < 4; ++r) {
            const Shape& shape = *table.shape(Registry::id(s, r));
            CAPTURE(shape.name);
            CAPTURE(r);
            VoxelChunk c;
            c.set(4, 5, 6, Registry::id(s, r));
            MeshOptions o;
            o.voxelSize = 0.5f;
            MeshStats stats;
            const MeshData mesh = meshChunk(single(c), table, o, &stats);
            CHECK(stats.quads == 0);
            CHECK(stats.shapePolygons == shape.polygons.size());
            size_t triangles = 0;
            for (const auto& p : shape.polygons) triangles += p.corners.size() - 2;
            CHECK(mesh.indices.size() == triangles * 3);
            CHECK(meshVolume(mesh) == doctest::Approx(shape.volume * 0.125).epsilon(1e-4));
            for (const Vertex& v : mesh.vertices) {
                CHECK(glm::all(glm::greaterThanEqual(v.position, glm::vec3(2.0f, 2.5f, 3.0f))));
                CHECK(glm::all(glm::lessThanEqual(v.position, glm::vec3(2.5f, 3.0f, 3.5f))));
                CHECK(glm::length(v.normal) == doctest::Approx(1.0f));
                CHECK(glm::dot(glm::vec3(v.tangent), v.normal) == doctest::Approx(0.0f).epsilon(1e-5));
                CHECK(v.color.r == doctest::Approx(0.8f)); // material 2, unoccluded
            }
            REQUIRE(mesh.submeshes.size() == 1);
            CHECK(sameBytes(mesh, meshChunk(single(c), table, o))); // deterministic
        }
    }
}

TEST_CASE("shape mesher: faces are culled only against fully covering neighbours") {
    const Registry reg;
    SUBCASE("slab on a cube: both touching faces disappear") {
        VoxelChunk c;
        c.set(3, 3, 3, reg.cube);
        c.set(3, 4, 3, reg.id("slab_low"));
        const MeshData m = meshChunk(single(c), reg.palette, {});
        CHECK(countFaces(m, {0, 1, 0}, 1, 4.0f) == 0);  // cube top
        CHECK(countFaces(m, {0, -1, 0}, 1, 4.0f) == 0); // slab bottom
        CHECK(countFaces(m, {0, 1, 0}, 1, 4.5f) == 2);  // slab top (interior polygon)
    }
    SUBCASE("cube next to a partial face stays visible; the partial face is hidden by the cube") {
        VoxelChunk c;
        c.set(3, 3, 2, reg.cube);
        c.set(3, 3, 3, reg.id("stair")); // stair front (-Z) is partial
        const MeshData m = meshChunk(single(c), reg.palette, {});
        CHECK(countFaces(m, {0, 0, 1}, 2, 3.0f) == 2);  // cube +Z face kept
        CHECK(countFaces(m, {0, 0, -1}, 2, 3.0f) == 0); // stair front culled
    }
    SUBCASE("two partial faces against each other are both kept") {
        VoxelChunk c;
        c.set(3, 3, 3, reg.id("slab_low"));
        c.set(4, 3, 3, reg.id("slab_low"));
        const MeshData m = meshChunk(single(c), reg.palette, {});
        CHECK(countFaces(m, {1, 0, 0}, 0, 4.0f) == 2);
        CHECK(countFaces(m, {-1, 0, 0}, 0, 4.0f) == 2);
    }
    SUBCASE("rotation moves the full face: stair turned once is full on +X") {
        VoxelChunk c;
        c.set(3, 3, 3, reg.id("stair", 1));
        c.set(4, 3, 3, reg.cube);
        const MeshData m = meshChunk(single(c), reg.palette, {});
        CHECK(countFaces(m, {1, 0, 0}, 0, 4.0f) == 0);
        CHECK(countFaces(m, {-1, 0, 0}, 0, 4.0f) == 0);
    }
}

TEST_CASE("shape mesher: culling works across chunk boundaries") {
    const Registry reg;
    VoxelChunk center;
    VoxelChunk east;
    center.set(31, 3, 3, reg.id("stair", 1)); // full +X face on the chunk boundary
    east.set(0, 3, 3, reg.cube);
    ChunkNeighborhood nb;
    nb.chunks[13] = &center;
    nb.chunks[ChunkNeighborhood::slot(1, 0, 0)] = &east;
    const MeshData m = meshChunk(nb, reg.palette, {});
    CHECK(countFaces(m, {1, 0, 0}, 0, 32.0f) == 0);
    ChunkNeighborhood nbEast;
    nbEast.chunks[13] = &east;
    nbEast.chunks[ChunkNeighborhood::slot(-1, 0, 0)] = &center;
    const MeshData e = meshChunk(nbEast, reg.palette, {});
    CHECK(countFaces(e, {-1, 0, 0}, 0, 0.0f) == 0); // cube face hidden by the stair's full face
    // Sealing exposes the shape's boundary polygon again.
    MeshOptions sealed;
    sealed.sealFaces = faceBit(int(Face::PosX));
    CHECK(countFaces(meshChunk(nb, reg.palette, sealed), {1, 0, 0}, 0, 32.0f) == 2);
}

TEST_CASE("shape mesher: ambient occlusion follows aoOccluder") {
    const Registry reg;
    const auto cubeTop = [&](Voxel diagonal) {
        VoxelChunk c;
        c.set(3, 3, 3, reg.cube);
        if (diagonal != kEmpty) c.set(4, 4, 3, diagonal);
        const MeshData m = meshChunk(single(c), reg.palette, {});
        float darkest = 1.0f;
        for (const Vertex& v : m.vertices) {
            if (v.normal.y > 0.5f && v.position.y == 4.0f) darkest = std::min(darkest, v.color.r / 0.5f);
        }
        return darkest;
    };
    CHECK(cubeTop(kEmpty) == 1.0f);
    CHECK(cubeTop(reg.id("slab_low")) == 1.0f); // not an occluder
    CHECK(cubeTop(reg.id("stair")) < 1.0f);     // occluder
    CHECK(cubeTop(reg.cube) < 1.0f);

    // A shape's polygons are darkened by occluding neighbours in front of them.
    VoxelChunk c;
    c.set(3, 3, 3, reg.id("slab_low"));
    c.set(4, 4, 3, reg.cube);
    const MeshData m = meshChunk(single(c), reg.palette, {});
    float darkest = 1.0f;
    for (const Vertex& v : m.vertices) {
        if (v.normal.y > 0.5f) darkest = std::min(darkest, v.color.r / 0.8f);
    }
    CHECK(darkest < 1.0f);
}

TEST_CASE("shape mesher: exact AO values on shape polygons follow the documented rule") {
    // Reference of the VOXELS §4 approximation: a polygon is shaded like a cube face along its
    // dominant normal axis (ties: Y, then X); each corner samples the side, side and corner
    // neighbours of the cell in front of it toward the nearest cell corner (corner coordinate
    // < 0.5 picks -1). Occluders sit only on edge/corner neighbours, so no polygon is culled and
    // the shape's vertices are the last ones of the mesh (cubes are emitted first).
    const Registry reg;
    const MeshOptions options;
    const glm::ivec3 cell(8, 8, 8);
    int checked = 0;
    for (uint16_t s = 1; s < reg.lib->size(); ++s) {
        for (int r = 0; r < 4; ++r) {
            for (uint64_t pattern : {0x5A5A5ull, 0xF0F0Full, 0x33333ull, 0xFFFFFull}) {
                VoxelChunk c;
                std::vector<glm::ivec3> occ;
                int bit = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (std::abs(dx) + std::abs(dy) + std::abs(dz) < 2) continue; // centre, face neighbours
                            if ((pattern >> bit++) & 1u) occ.push_back(cell + glm::ivec3(dx, dy, dz));
                        }
                    }
                }
                for (const glm::ivec3& o : occ) c.set(o.x, o.y, o.z, reg.cube);
                const Voxel id = Registry::id(s, r);
                c.set(cell.x, cell.y, cell.z, id);
                const MeshData m = meshChunk(single(c), reg.palette, options);
                const Shape shape = rotateShape((*reg.lib)[s], r);
                size_t corners = 0;
                for (const auto& p : shape.polygons) corners += p.corners.size();
                REQUIRE(m.vertices.size() >= corners);
                size_t vi = m.vertices.size() - corners;
                const auto occludes = [&](const glm::ivec3& q) {
                    for (const glm::ivec3& o : occ) {
                        if (o == q) return true;
                    }
                    return false;
                };
                for (const auto& p : shape.polygons) {
                    const glm::vec3 an = glm::abs(p.normal);
                    const int a = (an.y >= an.x && an.y >= an.z) ? 1 : an.x >= an.z ? 0 : 2;
                    const int u = (a + 1) % 3;
                    const int v = (a + 2) % 3;
                    glm::ivec3 front = cell;
                    front[a] += p.normal[a] > 0.0f ? 1 : -1;
                    for (const glm::vec3& corner : p.corners) {
                        glm::ivec3 du(0), dv(0);
                        du[u] = corner[u] < 0.5f ? -1 : 1;
                        dv[v] = corner[v] < 0.5f ? -1 : 1;
                        const bool s1 = occludes(front + du);
                        const bool s2 = occludes(front + dv);
                        const bool sc = occludes(front + du + dv);
                        const int ao = (s1 && s2) ? 0 : 3 - (int(s1) + int(s2) + int(sc));
                        const Vertex& vert = m.vertices[vi++];
                        CAPTURE(shape.name);
                        CAPTURE(r);
                        REQUIRE(vert.position == glm::vec3(cell) + corner);
                        CHECK(vert.color.r == 0.8f * options.aoCurve[size_t(ao)]);
                        ++checked;
                    }
                }
            }
        }
    }
    CHECK(checked > 1000);
}

TEST_CASE("shape mesher: texture layers and uvPerMetre") {
    const Registry reg;
    VoxelChunk c;
    c.set(2, 2, 2, reg.id("wedge", 2));
    c.set(6, 2, 2, reg.cube);
    MeshOptions o;
    o.voxelSize = 0.5f;
    const MeshData legacyUv = meshChunk(single(c), reg.palette, o);
    for (const Vertex& v : legacyUv.vertices) {
        const int a = std::abs(v.normal.y) >= std::abs(v.normal.x) && std::abs(v.normal.y) >= std::abs(v.normal.z) ? 1
                      : std::abs(v.normal.x) >= std::abs(v.normal.z)                                               ? 0
                                                                                                                     : 2;
        const glm::vec3 voxel = v.position / 0.5f;
        CHECK(v.uv0 == glm::vec2(voxel[(a + 1) % 3], voxel[(a + 2) % 3])); // voxel units
        const bool cube = v.position.x >= 3.0f;
        const float expected = a != 1 ? (cube ? 2.0f : 5.0f) : v.normal.y > 0 ? (cube ? 1.0f : 4.0f) : (cube ? 3.0f : 6.0f);
        CHECK(v.color.a == expected);
    }
    o.uvPerMetre = 1.0f; // one tile per metre: uv = position in metres
    const MeshData metric = meshChunk(single(c), reg.palette, o);
    REQUIRE(metric.vertices.size() == legacyUv.vertices.size());
    for (size_t i = 0; i < metric.vertices.size(); ++i) {
        CHECK(metric.vertices[i].uv0 == legacyUv.vertices[i].uv0 * 0.5f);
    }
}

TEST_CASE("shape mesher: sloped polygons take UVs and face layers from their dominant axis") {
    // VOXELS §4: a sloped polygon is projected along the dominant axis of its normal; ties prefer
    // Y, then X (a 45° wedge slope is a top face). Planar projection: along a 45° slope the
    // texture stretches by sqrt(2).
    const Registry reg;
    for (const char* name : {"wedge", "corner_outer", "corner_inner", "quarter_round", "quarter_round_inner"}) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            CAPTURE(name);
            CAPTURE(rotation);
            VoxelChunk c;
            c.set(3, 3, 3, reg.id(name, rotation));
            MeshOptions o;
            o.voxelSize = 0.5f;
            o.uvPerMetre = 2.0f; // one tile per cell: uv = position in cells
            const MeshData m = meshChunk(single(c), reg.palette, o);
            size_t sloped = 0;
            for (const Vertex& v : m.vertices) {
                const glm::vec3 n = glm::abs(v.normal);
                const bool axisAligned = (n.x == 1.0f) + (n.y == 1.0f) + (n.z == 1.0f) == 1;
                if (axisAligned) continue;
                ++sloped;
                const int a = n.y >= n.x && n.y >= n.z ? 1 : n.x >= n.z ? 0 : 2;
                const glm::vec3 cells = v.position / 0.5f;
                CHECK(v.uv0 == glm::vec2(cells[(a + 1) % 3], cells[(a + 2) % 3]));
                const float layer = a != 1 ? 5.0f : v.normal.y > 0.0f ? 4.0f : 6.0f; // material 2: {4, 5, 6}
                CHECK(v.color.a == layer);
            }
            CHECK(sloped > 0);
        }
    }
    // The wedge slope is an exact Y/Z tie and resolves to Y (top layer, uv = (z, x)).
    VoxelChunk c;
    c.set(0, 0, 0, reg.id("wedge", 0));
    MeshOptions o;
    o.uvPerMetre = 1.0f;
    const MeshData m = meshChunk(single(c), reg.palette, o);
    size_t ties = 0;
    for (const Vertex& v : m.vertices) {
        if (v.normal.x != 0.0f || v.normal.y == 0.0f || v.normal.z == 0.0f) continue;
        CHECK(std::abs(v.normal.y) == std::abs(v.normal.z));
        CHECK(v.uv0 == glm::vec2(v.position.z, v.position.x));
        CHECK(v.color.a == 4.0f);
        ++ties;
    }
    CHECK(ties > 0);
}

TEST_CASE("shape mesher: unknown registry ids mesh as fallback cubes, never as a real material") {
    // Registry with one block; id 2 is unknown although material 2 exists (V1 would have used it).
    VoxelPalette p;
    p.colors = {glm::vec4(0), glm::vec4(0.5f, 0.5f, 0.5f, 1), glm::vec4(0.8f, 0.6f, 0.4f, 1)};
    p.faceLayers = {{0, 0, 0}, {1, 2, 3}, {4, 5, 6}};
    p.blocks = {BlockState{}, BlockState{1, ShapeLibrary::kCube, 0, 0}};
    VoxelChunk c;
    c.set(4, 4, 4, 2);
    const MeshData m = meshChunk(single(c), p, {});
    REQUIRE(m.vertices.size() == 24); // a full cube
    for (const Vertex& v : m.vertices) {
        CHECK(v.color.r > 0.0f);
        CHECK(v.color.g == 0.0f); // magenta fallback, not material 2's colour
        CHECK(v.color.b == v.color.r);
        CHECK(v.color.a == 0.0f); // texture layer 0
    }
}

TEST_CASE("shape mesher: shapes count by fill and render as cubes at LOD > 0") {
    const Registry reg;
    const auto lodMesh = [&](Voxel v) {
        VoxelChunk c;
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                for (int x = 0; x < 2; ++x) c.set(x + 4, y + 4, z + 4, v);
        MeshOptions o;
        o.lod = 1;
        MeshStats stats;
        const MeshData m = meshChunk(single(c), reg.palette, o, &stats);
        CHECK(stats.shapePolygons == 0);
        return stats.quads;
    };
    CHECK(lodMesh(reg.id("slab_low")) == 6);   // 8 x 4/8 = half: solid cube cell
    CHECK(lodMesh(reg.id("panel_thin")) == 0); // 8 x 1/8: below half
    CHECK(lodMesh(reg.cube) == 6);
}

TEST_CASE("shape mesher: registry cubes mesh like the legacy palette and merge across rotations") {
    const Registry reg;
    VoxelChunk legacyChunk;
    VoxelChunk registryChunk;
    for (int x = 0; x < 8; ++x) {
        for (int z = 0; z < 8; ++z) {
            const bool second = (x + z) % 3 == 0;
            legacyChunk.set(x, 0, z, second ? Voxel(3) : Voxel(1));
            registryChunk.set(x, 0, z, second ? reg.cube2 : reg.cube);
        }
    }
    VoxelPalette legacy;
    legacy.colors = reg.palette.colors;
    legacy.faceLayers = reg.palette.faceLayers;
    CHECK(sameBytes(meshChunk(single(legacyChunk), legacy, {}), meshChunk(single(registryChunk), reg.palette, {})));

    // Cube ids that differ only in rotation share a material and merge into one quad per face.
    VoxelPalette rotated = reg.palette;
    rotated.blocks.push_back({1, ShapeLibrary::kCube, 2, 0});
    const auto rotatedCube = static_cast<Voxel>(rotated.blocks.size() - 1);
    VoxelChunk c;
    c.set(0, 0, 0, reg.cube);
    c.set(1, 0, 0, rotatedCube);
    MeshStats stats;
    meshChunk(single(c), rotated, {}, &stats);
    CHECK(stats.quads == 6);
}

namespace {

// Chunks with y <= fillTopChunk are uniformly `fill`; a single `floor` layer at world y = 0 when
// `floorOnly`.
class UniformGenerator final : public IVoxelGenerator {
public:
    UniformGenerator(Voxel fill, int fillTopChunk, bool floorOnly)
        : m_fill(fill), m_top(fillTopChunk), m_floorOnly(floorOnly) {}
    void generate(const ChunkCoord& coord, VoxelChunk& out) const override {
        if (m_floorOnly) {
            if (coord.y == 0) out.fillLayer(0, m_fill);
            return;
        }
        if (coord.y <= m_top) out = VoxelChunk(m_fill);
    }

private:
    Voxel m_fill;
    int m_top;
    bool m_floorOnly;
};

StreamingSettings tinyWorld() {
    StreamingSettings s;
    s.viewRadius = 2;
    s.minChunkY = -1;
    s.maxChunkY = 1;
    s.lodDistances = {10.0f};
    s.maxJobsPerUpdate = 1000;
    s.maxJobsInFlight = 1000;
    return s;
}

std::map<ChunkCoord, MeshData> settleWorld(VoxelWorld& world, const glm::vec3& observer) {
    std::map<ChunkCoord, MeshData> live;
    for (int i = 0; i < 64 && (i == 0 || !world.isSettled()); ++i) {
        world.update(observer);
        world.finishPendingWork();
        for (ChunkMeshEvent& e : world.takeMeshEvents()) {
            if (e.type == ChunkMeshEvent::Type::Remove) live.erase(e.coord);
            else live[e.coord] = std::move(e.mesh);
        }
    }
    REQUIRE(world.isSettled());
    return live;
}

} // namespace

TEST_CASE("voxel world: chunks full of shapes are not treated as solid") {
    const Registry reg;
    VoxelWorld cubes(std::make_shared<UniformGenerator>(reg.cube, 0, false), reg.palette, tinyWorld());
    const auto cubeLive = settleWorld(cubes, {16, 40, 16});
    CHECK(cubeLive.count({0, -1, 0}) == 0); // buried cube chunk: nothing to draw

    VoxelWorld stairs(std::make_shared<UniformGenerator>(reg.id("stair"), 0, false), reg.palette, tinyWorld());
    const auto stairLive = settleWorld(stairs, {16, 40, 16});
    REQUIRE(stairLive.count({0, -1, 0}) == 1); // stairs leave openings: the chunk is meshed
    CHECK_FALSE(stairLive.at({0, -1, 0}).indices.empty());
}

TEST_CASE("voxel world: below-world bedrock is a cube even when id 1 is a shape") {
    const Registry base;
    VoxelPalette p = base.palette;
    p.blocks = {BlockState{}, BlockState{2, *base.lib->find("stair"), 0, 0}, BlockState{1, ShapeLibrary::kCube, 0, 0}};
    const Voxel cubeId = 2;
    StreamingSettings s = tinyWorld();
    s.minChunkY = 0; // the floor chunk sits directly on bedrock
    VoxelWorld world(std::make_shared<UniformGenerator>(cubeId, 0, true), p, s);
    CHECK(world.blocks().firstCube() == cubeId);
    const auto live = settleWorld(world, {16, 20, 16});
    REQUIRE(live.count({0, 0, 0}) == 1);
    CHECK(countFaces(live.at({0, 0, 0}), {0, -1, 0}, 1, 0.0f) == 0); // floor underside culled
}

TEST_CASE("voxel world: shape worlds mesh identically with worker threads") {
    const Registry reg;
    StreamingSettings s = tinyWorld();
    s.lodDistances = {1.5f};
    const auto gen = std::make_shared<UniformGenerator>(reg.id("wedge", 3), 0, false);
    VoxelWorld inlineWorld(gen, reg.palette, s);
    const auto expected = settleWorld(inlineWorld, {16, 40, 16});
    JobSystem jobs(4);
    VoxelWorld threaded(gen, reg.palette, s, &jobs);
    const auto actual = settleWorld(threaded, {16, 40, 16});
    REQUIRE(actual.size() == expected.size());
    for (const auto& [coord, mesh] : expected) {
        REQUIRE(actual.count(coord) == 1);
        CHECK(sameBytes(actual.at(coord), mesh));
    }
}
