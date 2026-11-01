// Phase V2: block definition files (*.blocks.json) and the block catalogue. CPU only.
#include "SceneFile/BlockFile.h"

#include "Core/Error.h"
#include "Scene/Voxel/ChunkMesher.h"
#include "../Support/MeshHash.h"

#include <doctest/doctest.h>

#include <limits>
#include <string>
#include <vector>

using namespace zodar;
using namespace zodar::voxel;

namespace {

const char* kBlocks = R"({"schema": "zodar.blocks", "version": 1, "uvPerMetre": 1,
  "materials": [
    {"name": "concrete", "textures": "textures/concrete.png", "roughness": 0.9},
    {"name": "panel", "textures": {"top": "textures/panel_top.png", "all": "textures/panel.png"},
     "tint": [0.5, 0.75, 1], "metallic": 0.6, "roughness": 0.3, "emissive": 2.5},
    {"name": "concrete_wet", "textures": {"all": "textures/concrete.png"}, "roughness": 0.2}
  ],
  "blocks": [
    {"name": "concrete", "material": "concrete"},
    {"name": "stair", "material": "concrete", "shape": "stair", "rotations": 4},
    {"name": "grate", "material": "panel", "shape": "slab_low", "collision": false},
    {"name": "puddle", "material": "concrete_wet", "shape": "slab_low"}
  ]})";

std::vector<DataIssue> issuesOf(std::string_view json, const ShapeLibrary* shapes = nullptr) {
    try {
        (void)parseBlockFile(json, "game/blocks/test.blocks.json", shapes);
    } catch (const DataError& e) {
        return e.issues();
    }
    return {};
}

bool hasIssue(const std::vector<DataIssue>& issues, std::string_view path, std::string_view text) {
    for (const DataIssue& i : issues) {
        if (i.path == path && i.message.find(text) != std::string::npos) return true;
    }
    return false;
}

std::string wrap(const std::string& materials, const std::string& blocks) {
    return R"({"schema": "zodar.blocks", "version": 1, "materials": [)" + materials + R"(], "blocks": [)" + blocks +
           "]}";
}

} // namespace

TEST_CASE("block file: parse, ids in file order and @N rotation variants") {
    const ShapeLibrary lib = ShapeLibrary::builtins();
    const BlockFileData data = parseBlockFile(kBlocks, "blocks.json", &lib);
    REQUIRE(data.materials.size() == 3);
    CHECK(data.materials[1].textures ==
          std::array<std::string, 3>{"textures/panel_top.png", "textures/panel.png", "textures/panel.png"});
    CHECK(data.materials[1].tint == glm::vec3(0.5f, 0.75f, 1.0f));
    CHECK(data.materials[0].metallic == 0.0f); // defaults
    CHECK(data.materials[0].emissive == 0.0f);
    REQUIRE(data.blocks.size() == 4);
    CHECK(data.blocks[0].shape == "cube");
    CHECK(data.blocks[1].rotations == 4);
    CHECK_FALSE(data.blocks[2].collision);

    const BlockCatalogue c = buildBlockCatalogue(data, "game/blocks");
    // id 0 empty, then file order; rotations expand into consecutive ids.
    CHECK(c.names == std::vector<std::string>{"", "concrete", "stair", "stair@1", "stair@2", "stair@3", "grate",
                                              "puddle"});
    CHECK(c.find("concrete") == Voxel(1));
    CHECK(c.find("stair@2") == Voxel(4));
    CHECK(c.find("grate") == Voxel(6));
    CHECK_FALSE(c.find("stair@4").has_value());
    CHECK_FALSE(c.find("nope").has_value());
    REQUIRE(c.palette.blocks.size() == 8);
    CHECK(c.palette.blocks[4].rotation == 2);
    CHECK(c.palette.blocks[4].shape == *c.palette.shapes->find("stair"));
    CHECK(c.palette.blocks[6].flags == BlockState::kNoCollision);
    CHECK(c.palette.blocks[7].material == 2);
    CHECK(c.uvPerMetre == 1.0f);

    // Layers: first-use order, deduplicated by texture + parameters (concrete_wet reuses the
    // concrete texture with other parameters: its own layer).
    REQUIRE(c.layers.size() == 4);
    CHECK(c.layers[0] == std::filesystem::path("game/blocks/textures/concrete.png"));
    CHECK(c.layers[1] == std::filesystem::path("game/blocks/textures/panel_top.png"));
    CHECK(c.layers[2] == std::filesystem::path("game/blocks/textures/panel.png"));
    CHECK(c.layers[3] == c.layers[0]);
    CHECK(c.layerParams[1] == BlockLayerParams{0.3f, 0.6f, 2.5f});
    CHECK(c.layerParams[3] == BlockLayerParams{0.2f, 0.0f, 0.0f});
    CHECK(c.layerOrigins == std::vector<std::string>{"/materials/0/textures/top", "/materials/1/textures/top",
                                                     "/materials/1/textures/side", "/materials/2/textures/top"});
    CHECK(c.palette.faceLayers ==
          std::vector<std::array<uint16_t, 3>>{{0, 0, 0}, {1, 2, 2}, {3, 3, 3}});
    CHECK(c.palette.colors[1] == glm::vec4(0.5f, 0.75f, 1.0f, 1.0f));

    // The catalogue palette equals a hand-built registry palette.
    VoxelPalette hand;
    hand.colors = {glm::vec4(1.0f), glm::vec4(0.5f, 0.75f, 1.0f, 1.0f), glm::vec4(1.0f)};
    hand.faceLayers = {{0, 0, 0}, {1, 2, 2}, {3, 3, 3}};
    const uint16_t stair = *lib.find("stair");
    const uint16_t slab = *lib.find("slab_low");
    hand.blocks = {BlockState{},          {0, ShapeLibrary::kCube, 0, 0}, {0, stair, 0, 0}, {0, stair, 1, 0},
                   {0, stair, 2, 0},      {0, stair, 3, 0},               {1, slab, 0, BlockState::kNoCollision},
                   {2, slab, 0, 0}};
    CHECK(c.palette.colors == hand.colors);
    CHECK(c.palette.faceLayers == hand.faceLayers);
    REQUIRE(c.palette.blocks.size() == hand.blocks.size());
    for (size_t i = 0; i < hand.blocks.size(); ++i) {
        CAPTURE(i);
        CHECK(c.palette.blocks[i].material == hand.blocks[i].material);
        CHECK(c.palette.blocks[i].shape == hand.blocks[i].shape);
        CHECK(c.palette.blocks[i].rotation == hand.blocks[i].rotation);
        CHECK(c.palette.blocks[i].flags == hand.blocks[i].flags);
    }
    CHECK(blockFileTextures(data, "game/blocks") ==
          std::vector<std::filesystem::path>{"game/blocks/textures/concrete.png", "game/blocks/textures/panel.png",
                                             "game/blocks/textures/panel_top.png"});
}

TEST_CASE("block file: deterministic round-trip and catalogue") {
    const BlockFileData data = parseBlockFile(kBlocks, "a.blocks.json");
    const std::string text = serializeBlockFile(data);
    const BlockFileData back = parseBlockFile(text, "a.blocks.json");
    CHECK(back == data);
    CHECK(serializeBlockFile(back) == text);
    const BlockCatalogue a = buildBlockCatalogue(data, "dir");
    const BlockCatalogue b = buildBlockCatalogue(back, "dir");
    CHECK(a.names == b.names);
    CHECK(a.layers == b.layers);
    CHECK(a.layerParams == b.layerParams);
    CHECK(a.palette.colors == b.palette.colors);
    CHECK(a.palette.faceLayers == b.palette.faceLayers);

    // Id stability (VOXELS §3): appending keeps every existing id; inserting shifts later ids.
    BlockFileData appended = data;
    appended.blocks.push_back({"lamp", "panel", "cube", 1, true});
    const BlockCatalogue ap = buildBlockCatalogue(appended, "dir");
    for (size_t id = 0; id < a.names.size(); ++id) CHECK(ap.names[id] == a.names[id]);
    CHECK(ap.find("lamp") == Voxel(8));
    BlockFileData inserted = data;
    inserted.blocks.insert(inserted.blocks.begin(), {"lamp", "panel", "cube", 1, true});
    CHECK(buildBlockCatalogue(inserted, "dir").find("concrete") == Voxel(2));
}

TEST_CASE("block file: schema errors carry JSON paths") {
    const ShapeLibrary lib = ShapeLibrary::builtins();
    const auto bad = issuesOf(
        R"({"schema": "zodar.blockz", "version": 2, "uvPerMetre": 0, "extra": true,
            "materials": [
              {"name": "a", "textures": "a.png", "roughness": 1.5},
              {"name": "a", "textures": {"top": "t.png"}, "glow": 1},
              {"name": "b@1", "textures": "/abs/b.png", "tint": [2, 0, 0]}
            ],
            "blocks": [
              {"name": "x", "material": "missing"},
              {"name": "x", "material": "a", "shape": "dodecahedron", "rotations": 3},
              {"name": "y@2", "material": "a", "collision": "no"},
              {"material": "a"}
            ]})",
        &lib);
    CHECK(hasIssue(bad, "/schema", "expected \"zodar.blocks\""));
    CHECK(hasIssue(bad, "/version", "unsupported version 2"));
    CHECK(hasIssue(bad, "/uvPerMetre", "positive"));
    CHECK(hasIssue(bad, "/extra", "unknown field"));
    CHECK(hasIssue(bad, "/materials/0/roughness", "outside [0, 1]"));
    CHECK(hasIssue(bad, "/materials/1/name", "duplicate material name 'a'"));
    CHECK(hasIssue(bad, "/materials/1/glow", "unknown field"));
    CHECK(hasIssue(bad, "/materials/1/textures/side", "required field is missing (or give \"all\")"));
    CHECK(hasIssue(bad, "/materials/1/textures/bottom", "required field is missing"));
    CHECK(hasIssue(bad, "/materials/2/name", "contains '@'"));
    CHECK(hasIssue(bad, "/materials/2/textures", "must be relative"));
    CHECK(hasIssue(bad, "/materials/2/tint/0", "outside [0, 1]"));
    CHECK(hasIssue(bad, "/blocks/0/material", "unknown material 'missing'"));
    CHECK(hasIssue(bad, "/blocks/1/name", "duplicate block name 'x'"));
    CHECK(hasIssue(bad, "/blocks/1/shape", "unknown shape 'dodecahedron'"));
    CHECK(hasIssue(bad, "/blocks/1/rotations", "rotations must be 1 or 4, got 3"));
    CHECK(hasIssue(bad, "/blocks/2/name", "contains '@'"));
    CHECK(hasIssue(bad, "/blocks/2/collision", "expected true or false"));
    CHECK(hasIssue(bad, "/blocks/3/name", "required field is missing"));
    for (const DataIssue& i : bad) {
        CHECK_MESSAGE(i.line > 0, i.path);
        CHECK(i.file == "game/blocks/test.blocks.json");
    }
    CHECK(hasIssue(issuesOf(R"({"schema": "zodar.blocks", "materials": [], "blocks": []})"), "/version",
                   "required field is missing"));
    CHECK(hasIssue(issuesOf(R"({"schema": "zodar.blocks", "version": 1, "blocks": []})"), "/materials",
                   "expected an array"));
    // Without a shape library, shape names are checked when the catalogue is built.
    const BlockFileData unchecked =
        parseBlockFile(wrap(R"({"name": "a", "textures": "a.png"})", R"({"name": "x", "material": "a", "shape": "blob"})"),
                       "u.blocks.json");
    try {
        (void)buildBlockCatalogue(unchecked, "dir", {}, "u.blocks.json");
        FAIL("expected DataError");
    } catch (const DataError& e) {
        REQUIRE(e.issues().size() == 1);
        CHECK(e.issues()[0].path == "/blocks/0/shape");
        CHECK(e.issues()[0].file == "u.blocks.json");
    }
    // Id limit: 65534 ids including rotation variants (id 0xFFFF stays outside the registry).
    std::string many;
    for (int i = 0; i < 16384; ++i) many += (i ? "," : "") + std::string(R"({"name": "b)") + std::to_string(i) +
                                           R"(", "material": "a", "rotations": 4})";
    CHECK(hasIssue(issuesOf(wrap(R"({"name": "a", "textures": "a.png"})", many)), "/blocks/16383",
                   "too many block ids"));
}

TEST_CASE("block file: texture-array dimensions are validated per layer with JSON paths") {
    const BlockCatalogue c = buildBlockCatalogue(parseBlockFile(kBlocks, "b.blocks.json"), "game/blocks");
    CHECK(checkBlockLayerSizes(c, {{64, 64}, {64, 64}, {64, 64}, {64, 64}}, "b.blocks.json").empty());
    const auto issues = checkBlockLayerSizes(c, {{64, 64}, {32, 32}, {64, 32}, {64, 64}}, "b.blocks.json");
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].path == "/materials/1/textures/top");
    CHECK(issues[0].message ==
          "32x32 (panel_top.png) differs from the texture array size 64x64 (first layer: /materials/0/textures/top)");
    CHECK(issues[0].file == "b.blocks.json");
    CHECK(issues[1].path == "/materials/1/textures/side");
    CHECK(issues[1].message == "64x32 (panel.png) is not square (texture-array layers are square)");
    CHECK_THROWS_AS((void)checkBlockLayerSizes(c, {{64, 64}}, "b.blocks.json"), Error);
}

TEST_CASE("block file: catalogue palettes mesh deterministically (golden hash)") {
    const BlockCatalogue c = buildBlockCatalogue(parseBlockFile(kBlocks, "b.blocks.json"), "dir");
    VoxelChunk chunk;
    for (Voxel id = 1; id < c.palette.blocks.size(); ++id) {
        chunk.set(2 + 3 * id, 4, 5, id);
        chunk.set(2 + 3 * id, 5, 5, 1); // a cube on top: culling against shapes
        chunk.set(2 + 3 * id, 4, 6, id);
    }
    chunk.set(30, 10, 10, static_cast<Voxel>(c.palette.blocks.size() + 3)); // unknown: fallback cube
    ChunkNeighborhood nb;
    nb.chunks[13] = &chunk;
    MeshOptions o;
    o.voxelSize = 0.5f;
    o.uvPerMetre = c.uvPerMetre;
    test::Fnv h;
    test::hashMesh(h, meshChunk(nb, c.palette, o));
    const uint64_t first = h.get();
    test::Fnv again;
    test::hashMesh(again, meshChunk(nb, buildBlockCatalogue(parseBlockFile(kBlocks, "b.blocks.json"), "dir").palette, o));
    CHECK(again.get() == first);
    // Golden: captured on GCC 13 (V2); integer-only inputs, single IEEE operations.
    CHECK(first == 0x2444B9C0D80D2BD6ull);
}

TEST_CASE("block file: catalogues built from code data are validated with JSON paths") {
    const BlockFileData good = parseBlockFile(kBlocks, "b.blocks.json");
    const auto issuesOfBuild = [](const BlockFileData& d) {
        try {
            (void)buildBlockCatalogue(d, "dir", {}, "code");
        } catch (const DataError& e) {
            return e.issues();
        }
        return std::vector<DataIssue>{};
    };
    CHECK(issuesOfBuild(good).empty());

    BlockFileData bad = good;
    bad.blocks[0].rotations = 0;
    bad.blocks[1].rotations = 2;
    bad.blocks[2].name = "";
    bad.blocks[3].name = "concrete"; // duplicate of blocks[0]
    bad.materials[1].name = "concrete"; // duplicate of materials[0]
    bad.materials[2].name = "";
    bad.materials[0].roughness = std::numeric_limits<float>::quiet_NaN();
    bad.materials[0].metallic = 2.0f;
    bad.materials[1].emissive = -1.0f;
    bad.materials[1].tint.y = std::numeric_limits<float>::quiet_NaN();
    bad.materials[2].textures[2] = "";
    bad.uvPerMetre = 0.0f;
    const auto issues = issuesOfBuild(bad);
    CHECK(hasIssue(issues, "/blocks/0/rotations", "rotations must be 1 or 4, got 0"));
    CHECK(hasIssue(issues, "/blocks/1/rotations", "got 2"));
    CHECK(hasIssue(issues, "/blocks/2/name", "empty block name"));
    CHECK(hasIssue(issues, "/blocks/3/name", "duplicate block name 'concrete'"));
    CHECK(hasIssue(issues, "/materials/1/name", "duplicate material name 'concrete'"));
    CHECK(hasIssue(issues, "/materials/2/name", "empty material name"));
    CHECK(hasIssue(issues, "/materials/0/roughness", "outside [0"));
    CHECK(hasIssue(issues, "/materials/0/metallic", "outside [0"));
    CHECK(hasIssue(issues, "/materials/1/emissive", "outside [0"));
    CHECK(hasIssue(issues, "/materials/1/tint/1", "outside [0, 1]"));
    CHECK(hasIssue(issues, "/materials/2/textures/bottom", "empty texture path"));
    CHECK(hasIssue(issues, "/uvPerMetre", "positive"));
    for (const DataIssue& i : issues) CHECK(i.file == "code");

    // Id limit: 65534 ids fit (id 0xFFFF stays outside every registry), 65535 do not.
    BlockFileData many;
    many.materials = {good.materials[0]};
    for (int i = 0; i < 16383; ++i) many.blocks.push_back({"r" + std::to_string(i), "concrete", "cube", 4, true});
    many.blocks.push_back({"x", "concrete", "cube", 1, true});
    many.blocks.push_back({"y", "concrete", "cube", 1, true}); // 16383 * 4 + 2 = 65534 ids
    const BlockCatalogue full = buildBlockCatalogue(many, "dir");
    CHECK(full.palette.blocks.size() == 0xFFFF);
    CHECK(full.find("y") == Voxel(0xFFFE));
    const voxel::BlockTable table(full.palette);
    CHECK(table.isCube(0xFFFF)); // outside the registry: fallback cube
    many.blocks.push_back({"z", "concrete", "cube", 1, true});
    CHECK(hasIssue(issuesOfBuild(many), "/blocks", "too many block ids (65535; at most 65534"));
    std::string json = R"({"schema": "zodar.blocks", "version": 1, "materials": [{"name": "a", "textures": "a.png"}],
        "blocks": [)";
    for (int i = 0; i < 16383; ++i) json += R"({"name": "r)" + std::to_string(i) + R"(", "material": "a", "rotations": 4},)";
    CHECK(issuesOf(json + R"({"name": "x", "material": "a"}, {"name": "y", "material": "a"}]})").empty());
    CHECK(hasIssue(issuesOf(json + R"({"name": "x", "material": "a"}, {"name": "y", "material": "a"},
        {"name": "z", "material": "a"}]})"), "/blocks/16385", "too many block ids"));

    // BlockTable refuses registries that would use id 0xFFFF.
    voxel::VoxelPalette huge;
    huge.colors = {glm::vec4(1.0f)};
    huge.blocks.resize(0x10000);
    CHECK_THROWS_AS(voxel::BlockTable{huge}, Error);
}

TEST_CASE("block file: layer checks use a square reference layer and validate their inputs") {
    const BlockCatalogue c = buildBlockCatalogue(parseBlockFile(kBlocks, "b.blocks.json"), "game/blocks");
    // Layer 0 is not square: it is reported, and layer 1 (the first square one) is the reference.
    const auto issues = checkBlockLayerSizes(c, {{64, 32}, {32, 32}, {32, 32}, {64, 64}}, "b.blocks.json");
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].path == "/materials/0/textures/top");
    CHECK(issues[0].message.find("64x32 (concrete.png) is not square") == 0);
    CHECK(issues[1].path == "/materials/2/textures/top");
    CHECK(issues[1].message.find("differs from the texture array size 32x32 (first layer: /materials/1/textures/top)") !=
          std::string::npos);
    // No square layer at all: every layer is reported as not square, nothing else.
    CHECK(checkBlockLayerSizes(c, {{2, 1}, {2, 1}, {2, 1}, {2, 1}}, "b").size() == 4);
    // Mismatched origins (a hand-built catalogue) are an error, never an out-of-range read.
    BlockCatalogue broken = c;
    broken.layerOrigins.pop_back();
    CHECK_THROWS_AS((void)checkBlockLayerSizes(broken, {{64, 64}, {64, 64}, {64, 64}, {64, 64}}, "b"), Error);
    // Layer count against a texture-array limit.
    CHECK(checkBlockLayerCount(c, 4, "b").empty());
    const auto count = checkBlockLayerCount(c, 3, "b.blocks.json");
    REQUIRE(count.size() == 1);
    CHECK(count[0].path == "/materials");
    CHECK(count[0].message.find("4 texture-array layers exceed the limit of 3") == 0);
}
