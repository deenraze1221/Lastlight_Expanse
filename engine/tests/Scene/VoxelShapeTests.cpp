#include <doctest/doctest.h>

#include "Core/Error.h"
#include "Scene/Voxel/BlockRegistry.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

using namespace zodar;
using namespace zodar::voxel;

namespace {

constexpr auto N = FaceCoverage::None;
constexpr auto P = FaceCoverage::Partial;
constexpr auto F = FaceCoverage::Full;

struct Expected {
    std::array<FaceCoverage, 6> coverage; // NegX, PosX, NegY, PosY, NegZ, PosZ
    int fill;
    bool aoOccluder;
};

// Face that `face` becomes after one yaw step (+X turns into -Z).
int yawFace(int face) {
    static constexpr int map[6] = {int(Face::PosZ), int(Face::NegZ), int(Face::NegY),
                                   int(Face::PosY), int(Face::NegX), int(Face::PosX)};
    return map[face];
}

ShapeTemplate unitBox(const char* name) {
    ShapeTemplate t;
    t.name = name;
    const ShapeLibrary lib;
    for (const auto& p : lib[ShapeLibrary::kCube].polygons) t.polygons.push_back({p.corners});
    return t;
}

} // namespace

TEST_CASE("voxel shapes: built-in library contents and derived data") {
    const ShapeLibrary lib = ShapeLibrary::builtins();
    const std::map<std::string, Expected> expected = {
        {"cube", {{F, F, F, F, F, F}, 8, true}},
        {"slab_low", {{P, P, F, N, P, P}, 4, false}},
        {"slab_high", {{P, P, N, F, P, P}, 4, false}},
        {"stair", {{P, P, F, P, P, F}, 6, true}},
        {"wedge", {{P, P, F, N, N, F}, 4, false}},
        {"corner_inner", {{P, F, F, N, P, F}, 5, false}},
        {"corner_outer", {{N, P, F, N, N, P}, 3, false}},
        {"quarter_round", {{P, P, F, N, F, N}, 6, true}},
        {"quarter_round_inner", {{P, P, N, F, N, F}, 2, false}},
        {"panel_thin", {{P, P, P, P, F, N}, 1, false}},
    };
    REQUIRE(lib.size() == expected.size());
    CHECK(lib.find("cube") == ShapeLibrary::kCube);
    CHECK_FALSE(lib.find("quarter_block"));
    for (const auto& [name, e] : expected) {
        CAPTURE(name);
        const auto index = lib.find(name);
        REQUIRE(index);
        const Shape& s = lib[*index];
        CHECK(s.coverage == e.coverage);
        CHECK(int(s.fillEighths) == e.fill);
        CHECK(s.aoOccluder == e.aoOccluder);
        CHECK_FALSE(s.collision.empty());
        for (int f = 0; f < 6; ++f) {
            const uint16_t mask = s.coverageMask[size_t(f)];
            if (s.coverage[size_t(f)] == F) CHECK(mask == 0xFFFF);
            if (s.coverage[size_t(f)] == N) CHECK(mask == 0);
        }
    }
    CHECK(lib[*lib.find("slab_low")].volume == doctest::Approx(0.5f));
    CHECK(lib[*lib.find("stair")].volume == doctest::Approx(0.75f));
    CHECK(lib[*lib.find("corner_inner")].volume == doctest::Approx(2.0f / 3.0f));
    CHECK(lib[*lib.find("corner_outer")].volume == doctest::Approx(1.0f / 3.0f));
    CHECK(lib[*lib.find("panel_thin")].volume == doctest::Approx(0.125f));
    // 4-segment quarter circle: 2 * sin(22.5 deg).
    CHECK(lib[*lib.find("quarter_round")].volume == doctest::Approx(0.76536686f));
    CHECK(lib[*lib.find("quarter_round_inner")].volume == doctest::Approx(1.0f - 0.76536686f));
}

TEST_CASE("voxel shapes: partial coverage masks follow the face axes") {
    const ShapeLibrary lib = ShapeLibrary::builtins();
    // Face -X: u = Y (bit column i), v = Z (row j). Lower half in Y = columns 0 and 1.
    CHECK(lib[*lib.find("slab_low")].coverageMask[size_t(Face::NegX)] == 0x3333);
    CHECK(lib[*lib.find("slab_high")].coverageMask[size_t(Face::NegX)] == 0xCCCC);
    // Face -Z of the stair: u = X, v = Y: lower riser = rows 0 and 1.
    CHECK(lib[*lib.find("stair")].coverageMask[size_t(Face::NegZ)] == 0x00FF);
    // Stair side (-X): the full lower half plus the upper back quarter.
    CHECK(lib[*lib.find("stair")].coverageMask[size_t(Face::NegX)] == (0x3333 | 0xCC00));
}

TEST_CASE("voxel shapes: yaw rotation permutes faces and keeps volume") {
    const ShapeLibrary lib = ShapeLibrary::builtins();
    for (size_t i = 0; i < lib.size(); ++i) {
        const Shape& s = lib[static_cast<uint16_t>(i)];
        CAPTURE(s.name);
        Shape r = s;
        for (int step = 1; step <= 4; ++step) {
            const Shape next = rotateShape(s, step);
            for (int f = 0; f < 6; ++f) {
                CHECK(next.coverage[size_t(yawFace(f))] == r.coverage[size_t(f)]);
            }
            CHECK(next.volume == doctest::Approx(s.volume));
            CHECK(next.fillEighths == s.fillEighths);
            CHECK(next.collision.size() == s.collision.size());
            for (const ShapeBox& b : next.collision) {
                CHECK(glm::all(glm::lessThan(b.min, b.max)));
            }
            r = next;
        }
        CHECK(r.coverage == s.coverage);
        CHECK(r.coverageMask == s.coverageMask);
    }
    // One step turns the stair's full back (+Z) into a full +X face.
    const Shape stair1 = rotateShape(lib[*lib.find("stair")], 1);
    CHECK(stair1.fullFaces() == (faceBit(int(Face::NegY)) | faceBit(int(Face::PosX))));
    CHECK(rotateShape(lib[*lib.find("stair")], -1).fullFaces() == rotateShape(lib[*lib.find("stair")], 3).fullFaces());
}

TEST_CASE("voxel shapes: invalid templates are rejected") {
    ShapeLibrary lib;
    CHECK_THROWS_AS(lib.add(unitBox("cube")), Error); // duplicate name
    ShapeTemplate open = unitBox("open");
    open.polygons.pop_back();
    CHECK_THROWS_AS(lib.add(open), Error);
    ShapeTemplate inward = unitBox("inward");
    std::reverse(inward.polygons[0].corners.begin(), inward.polygons[0].corners.end());
    CHECK_THROWS_AS(lib.add(inward), Error);
    ShapeTemplate outside = unitBox("outside");
    for (auto& p : outside.polygons) for (auto& c : p.corners) c *= 1.5f;
    CHECK_THROWS_AS(lib.add(outside), Error);
    ShapeTemplate degenerate = unitBox("degenerate");
    degenerate.polygons[0].corners = {glm::vec3(0), glm::vec3(0), glm::vec3(0)};
    CHECK_THROWS_AS(lib.add(degenerate), Error);
    ShapeTemplate badBox = unitBox("bad_box");
    badBox.collision = {{glm::vec3(0.5f), glm::vec3(0.5f)}};
    CHECK_THROWS_AS(lib.add(badBox), Error);
    CHECK(lib.size() == 1);
    CHECK(lib.add(unitBox("box_copy")) == 1); // a valid custom shape registers
}

namespace {

// Message of the zodar::Error thrown by lib.add(t), or "" when it registers.
std::string addError(ShapeLibrary& lib, const ShapeTemplate& t) {
    try {
        lib.add(t);
    } catch (const Error& e) {
        return e.what();
    }
    return {};
}

// Polygon on the -Y cell face from (x, z) points, counter-clockwise in (x, z) = outward (-Y).
ShapePolygon bottom(std::initializer_list<glm::vec2> xz) {
    ShapePolygon p;
    for (const glm::vec2& q : xz) p.corners.emplace_back(q.x, 0.0f, q.y);
    return p;
}

} // namespace

TEST_CASE("voxel shapes: polygons must be star-shaped from their first corner") {
    ShapeLibrary lib;
    // L-shaped polygon starting at a corner that cannot see the whole polygon: one fan triangle
    // is reversed, so the fan would overlap itself.
    ShapeTemplate reversed{"l_from_bad_corner",
                           {bottom({{1, 0.25f}, {0.25f, 0.25f}, {0.25f, 1}, {0, 1}, {0, 0}, {1, 0}})}, {}, false};
    CHECK(addError(lib, reversed).find("not star-shaped from its first corner (fan triangle 0 is reversed)") !=
          std::string::npos);
    // The same L starting at a kernel corner passes the star test (and then fails later as an
    // open surface, which is not the star error).
    ShapeTemplate kernel{"l_from_kernel",
                         {bottom({{0, 0}, {1, 0}, {1, 0.25f}, {0.25f, 0.25f}, {0.25f, 1}, {0, 1}})}, {}, false};
    const std::string kernelError = addError(lib, kernel);
    CHECK(kernelError.find("star-shaped") == std::string::npos);
    CHECK(kernelError.find("not closed") != std::string::npos);
    // A fan that turns the right way at every step but wraps around its first corner twice.
    ShapePolygon spiral;
    spiral.corners.emplace_back(0.5f, 0.0f, 0.5f);
    for (int k = 0; k <= 8; ++k) {
        const float a = glm::radians(100.0f * static_cast<float>(k));
        spiral.corners.emplace_back(0.5f + 0.4f * std::cos(a), 0.0f, 0.5f + 0.4f * std::sin(a));
    }
    CHECK(addError(lib, ShapeTemplate{"spiral", {spiral}, {}, false}).find("the fan wraps around it") !=
          std::string::npos);
    CHECK(lib.size() == 1);
}

TEST_CASE("voxel shapes: overlapping face polygons are rejected even when areas sum to 1") {
    ShapeLibrary lib;
    // Two half-face strips that overlap by a quarter and leave a quarter uncovered: area 1.
    ShapeTemplate t{"overlap",
                    {bottom({{0, 0}, {1, 0}, {1, 0.5f}, {0, 0.5f}}), bottom({{0, 0.25f}, {1, 0.25f}, {1, 0.75f}, {0, 0.75f}})},
                    {},
                    false};
    CHECK(addError(lib, t).find("overlapping polygons on face 2") != std::string::npos);
    // Polygons that only share an edge (the diagonal of a split face) do not overlap.
    ShapeTemplate split = unitBox("split_bottom");
    split.polygons.erase(std::remove_if(split.polygons.begin(), split.polygons.end(),
                                        [](const ShapePolygon& p) {
                                            return std::all_of(p.corners.begin(), p.corners.end(),
                                                               [](const glm::vec3& c) { return c.y == 0.0f; });
                                        }),
                         split.polygons.end());
    REQUIRE(split.polygons.size() == 5);
    split.polygons.push_back(bottom({{0, 0}, {1, 0}, {1, 1}}));
    split.polygons.push_back(bottom({{0, 0}, {1, 1}, {0, 1}}));
    const uint16_t index = lib.add(split);
    CHECK(lib[index].coverage[static_cast<size_t>(Face::NegY)] == F);
    // Every built-in shape passes the stricter validation.
    CHECK(ShapeLibrary::builtins().size() == 10);
}

TEST_CASE("block table: legacy palettes are cubes with material = id") {
    VoxelPalette p;
    p.colors = {glm::vec4(0), glm::vec4(1), glm::vec4(0.5f)};
    const BlockTable t(p);
    CHECK(t.legacy());
    CHECK_FALSE(t.info(kEmpty).solid);
    CHECK(t.cellFlags(kEmpty) == 0);
    for (Voxel v : {Voxel(1), Voxel(2), Voxel(77)}) {
        const BlockInfo i = t.info(v);
        CHECK(i.solid);
        CHECK(i.cube);
        CHECK(i.aoOccluder);
        CHECK(i.fullFaces == kAllFaces);
        CHECK(i.fill == 8);
        CHECK(i.material == v);
        CHECK(i.shape == nullptr);
    }
    CHECK(t.firstCube() == 1);
}

TEST_CASE("block table: registry states resolve shape, rotation and material") {
    auto lib = std::make_shared<ShapeLibrary>(ShapeLibrary::builtins());
    VoxelPalette p;
    p.colors = {glm::vec4(0), glm::vec4(1), glm::vec4(0.5f)};
    p.shapes = lib;
    p.blocks.resize(5);
    p.blocks[1] = {2, *lib->find("stair"), 1, 0};
    p.blocks[2] = {1, ShapeLibrary::kCube, 0, BlockState::kNoCollision};
    p.blocks[3] = {1, *lib->find("stair"), 1, 0}; // same rotated shape, other material
    p.blocks[4] = {2, *lib->find("slab_low"), 0, 0};
    const BlockTable t(p);
    CHECK_FALSE(t.legacy());
    const BlockInfo stair = t.info(1);
    CHECK(stair.solid);
    CHECK_FALSE(stair.cube);
    CHECK(stair.material == 2);
    CHECK(stair.fill == 6);
    CHECK(stair.aoOccluder);
    CHECK(stair.fullFaces == (faceBit(int(Face::NegY)) | faceBit(int(Face::PosX))));
    REQUIRE(stair.shape != nullptr);
    CHECK(t.shape(3) == stair.shape); // shared rotated geometry
    CHECK(t.info(2).cube);
    CHECK(t.info(2).flags == BlockState::kNoCollision);
    CHECK_FALSE(t.info(4).aoOccluder);
    CHECK(t.info(4).fullFaces == faceBit(int(Face::NegY)));
    CHECK(t.firstCube() == 2);
    CHECK(t.info(9).cube); // beyond the registry: fallback cube
    CHECK(t.info(9).material == 9);

    VoxelPalette noCube = p;
    noCube.blocks = {BlockState{}, p.blocks[1]};
    CHECK(BlockTable(noCube).firstCube() == 2);
}

TEST_CASE("block table: invalid registries fail with clear errors") {
    auto lib = std::make_shared<ShapeLibrary>(ShapeLibrary::builtins());
    VoxelPalette p;
    p.colors = {glm::vec4(0), glm::vec4(1)};
    p.shapes = lib;
    p.blocks = {BlockState{}, BlockState{5, 0, 0, 0}};
    CHECK_THROWS_AS(BlockTable{p}, Error); // material out of range
    p.blocks = {BlockState{}, BlockState{1, 0, 4, 0}};
    CHECK_THROWS_AS(BlockTable{p}, Error); // rotation out of range
    p.blocks = {BlockState{}, BlockState{1, 99, 0, 0}};
    CHECK_THROWS_AS(BlockTable{p}, Error); // unknown shape
    p.shapes = nullptr;
    p.blocks = {BlockState{}, BlockState{1, 2, 0, 0}};
    CHECK_THROWS_AS(BlockTable{p}, Error); // shape without a library
}
