#include <doctest/doctest.h>

#include "Scene/Voxel/ChunkMesher.h"

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
