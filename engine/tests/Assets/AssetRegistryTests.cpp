// Phase C3: asset registry (assets.json), import, dependencies/hashes and checks. CPU only.
#include "AssetManager/AssetRegistry.h"
#include "Core/Error.h"
#include "Core/ImageIO.h"
#include "SceneFile/DataError.h"
#include "SceneFile/SceneFile.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace zodar;

namespace {

const std::filesystem::path kExample = std::filesystem::path(ZODAR_EXAMPLES_DIR) / "asset_loading";

// Temporary asset folder copied from examples/asset_loading.
struct Workspace {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "zodar_asset_registry_tests";
    Workspace() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        std::filesystem::copy(kExample, dir, std::filesystem::copy_options::recursive);
        std::filesystem::remove(dir / "assets.json");
    }
    ~Workspace() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void write(const std::string& name, const std::string& text) const {
        std::ofstream(dir / name, std::ios::binary) << text;
    }
};

std::vector<DataIssue> parseIssues(std::string_view json) {
    try {
        (void)AssetRegistry::parse(json, "reg/assets.json");
    } catch (const DataError& e) {
        return e.issues();
    }
    return {};
}

bool contains(const std::vector<std::string>& list, std::string_view text) {
    for (const std::string& s : list) {
        if (s.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("asset registry: types, default ids and loader settings") {
    CHECK((detectAssetType("a/B.PNG") == AssetType::Texture));
    CHECK((detectAssetType("m.glb") == AssetType::Model));
    CHECK((detectAssetType("x.material.json") == AssetType::Material));
    CHECK((detectAssetType("x.scene.json") == AssetType::Scene));
    CHECK((detectAssetType("x.prefab.json") == AssetType::Prefab));
    CHECK_FALSE(detectAssetType("notes.json").has_value());
    CHECK(defaultAssetId("dir/rune.material.json") == "rune");
    CHECK(defaultAssetId("crate.gltf") == "crate");
    CHECK((parseAssetType("prefab") == AssetType::Prefab));

    AssetEntry e;
    e.type = AssetType::Model;
    e.sampling = ModelSampling::PixelArt;
    e.compile = true;
    const ModelLoadSettings m = modelLoadSettings(e);
    REQUIRE(m.samplerOverride.has_value());
    CHECK(*m.samplerOverride == gfx::SamplerDesc::pixelArt());
    CHECK(m.useCompiledTextures);
    e.usage = TextureUsage::Normal;
    e.mips = false;
    CHECK((textureLoadSettings(e).usage == TextureUsage::Normal));
    CHECK_FALSE(textureLoadSettings(e).generateMips);
}

TEST_CASE("asset registry: parse, validation and deterministic round-trip") {
    AssetRegistry r("root");
    AssetEntry tex{"b_tex", AssetType::Texture, "t/b.png", TextureUsage::Data, false};
    tex.hash = "fnv1a64:0000000000000001";
    AssetEntry model{"a_model", AssetType::Model, "m.glb"};
    model.sampling = ModelSampling::Linear;
    model.compile = true;
    model.dependencies = {"t/b.png"};
    model.outputs = {"m.glb.zodar/image0_color.ktx2"};
    r.upsert(tex);
    r.upsert(model);
    REQUIRE(r.entries().size() == 2);
    CHECK(r.entries()[0].id == "a_model"); // sorted by id
    const AssetRegistry back = AssetRegistry::parse(r.serialize(), "root/assets.json");
    CHECK(back == r);
    CHECK(back.serialize() == r.serialize());
    CHECK(back.at("b_tex").mips == false);
    CHECK(back.directory() == std::filesystem::path("root"));
    CHECK_THROWS_AS((void)back.at("missing"), Error);
    CHECK(r.remove("b_tex"));
    CHECK_FALSE(r.remove("b_tex"));

    const auto issues = parseIssues(R"({"schema": "zodar.asset_registry", "version": 2, "assets": [
        {"id": "a", "type": "texture", "source": "a.png", "sampling": "linear"},
        {"id": "a", "type": "mesh", "source": "b.glb"},
        {"type": "model", "usage": "color"}
    ]})");
    const auto has = [&](std::string_view path, std::string_view text) {
        for (const DataIssue& i : issues) {
            if (i.path == path && i.message.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    CHECK(has("/version", "unsupported version 2"));
    CHECK(has("/assets/0/sampling", "unknown field"));
    CHECK(has("/assets/1/type", "unknown asset type 'mesh'"));
    CHECK(has("/assets/1/id", "duplicate asset id 'a'"));
    CHECK(has("/assets/2/id", "required field is missing"));
    CHECK(has("/assets/2/source", "required field is missing"));
    CHECK(has("/assets/2/usage", "unknown field"));
    CHECK(issues.front().line > 0);
    CHECK(parseIssues(R"({"schema": "other", "version": 1, "assets": []})").size() == 1);
}

TEST_CASE("asset registry: import records dependencies and hashes; checks find stale and missing files") {
    const Workspace ws;
    AssetRegistry r(ws.dir);
    const AssetEntry crate = importAsset(r, ws.dir / "crate.gltf");
    CHECK(crate.id == "crate");
    CHECK((crate.type == AssetType::Model));
    CHECK(crate.source == "crate.gltf");
    CHECK(crate.dependencies == std::vector<std::string>{"textures/crate.png"});
    CHECK(crate.hash.starts_with("fnv1a64:"));
    importAsset(r, ws.dir / "materials" / "rune.material.json");
    importAsset(r, ws.dir / "asset_loading.scene.json");
    CHECK(r.at("asset_loading").dependencies ==
          std::vector<std::string>{"crate.gltf", "materials/rune.material.json", "textures/crate.png",
                                   "textures/rune.png"}); // crate.gltf: modelRenderer by path
    // Default id collision (crate.png -> "crate") must be explicit.
    CHECK_THROWS_WITH_AS(importAsset(r, ws.dir / "textures" / "crate.png"), doctest::Contains("pass --id"), Error);
    AssetImportOptions texOptions;
    texOptions.id = "crate_texture";
    texOptions.usage = TextureUsage::Color;
    importAsset(r, ws.dir / "textures" / "crate.png", texOptions);
    // Re-import keeps id and settings; wrong-type settings and outside files are rejected.
    AssetImportOptions sampling;
    sampling.sampling = ModelSampling::PixelArt;
    CHECK(importAsset(r, ws.dir / "crate.gltf", sampling).id == "crate");
    CHECK((r.at("crate").sampling == ModelSampling::PixelArt));
    CHECK_THROWS_AS(importAsset(r, ws.dir / "textures" / "rune.png", sampling), Error);
    CHECK_THROWS_WITH_AS(importAsset(r, kExample / "crate.gltf"), doctest::Contains("outside the registry"), Error);
    CHECK_THROWS_AS(importAsset(r, ws.dir / "nope.png"), Error);
    CHECK(r.entries().size() == 4);

    r.save(ws.dir / "assets.json");
    const AssetRegistry loaded = AssetRegistry::load(ws.dir / "assets.json");
    CHECK(loaded == r);
    auto results = checkRegistry(loaded);
    for (const AssetCheckResult& res : results) {
        CHECK_MESSAGE(std::string(res.status()) == "ok", res.id);
    }
    const AssetCheckResult crateCheck = checkAsset(loaded, loaded.at("crate"));
    CHECK(crateCheck.triangles == 12);
    CHECK(crateCheck.vertices == 24);
    CHECK(crateCheck.texelDensity == doctest::Approx(32.0));
    REQUIRE(crateCheck.textures.size() == 1);
    CHECK((crateCheck.textures[0].sizeClass == TextureSizeClass::Default));

    // Editing a dependency makes every asset that uses it stale (warning, still usable).
    std::vector<uint8_t> red(32 * 32 * 4, 200);
    REQUIRE(writePng(ws.dir / "textures" / "crate.png", 32, 32, red));
    results = checkRegistry(loaded);
    CHECK(contains(checkAsset(loaded, loaded.at("crate")).warnings, "stale"));
    CHECK(contains(checkAsset(loaded, loaded.at("asset_loading")).warnings, "stale"));
    CHECK(checkAsset(loaded, loaded.at("rune")).warnings.empty());
    // Missing dependency and missing source are errors; size and density rules are warnings.
    std::filesystem::remove(ws.dir / "textures" / "rune.png");
    CHECK(contains(checkAsset(loaded, loaded.at("rune")).errors, "dependency missing: textures/rune.png"));
    std::filesystem::remove(ws.dir / "crate.gltf");
    CHECK(contains(checkAsset(loaded, loaded.at("crate")).errors, "source missing"));

    std::vector<uint8_t> odd(48 * 48 * 4, 100);
    REQUIRE(writePng(ws.dir / "textures" / "odd.png", 48, 48, odd));
    importAsset(r, ws.dir / "textures" / "odd.png");
    CHECK(contains(checkAsset(r, r.at("odd")).warnings, "48x48 is not a standard texture size"));
    // Above the 64x64 maximum: reported, never resized.
    std::vector<uint8_t> big(128 * 128 * 4, 50);
    REQUIRE(writePng(ws.dir / "textures" / "big.png", 128, 128, big));
    importAsset(r, ws.dir / "textures" / "big.png");
    const AssetCheckResult bigCheck = checkAsset(r, r.at("big"));
    CHECK(contains(bigCheck.warnings, "128x128 exceeds the 64x64 maximum"));
    REQUIRE(bigCheck.textures.size() == 1);
    CHECK(bigCheck.textures[0].width == 128);
    CHECK((bigCheck.textures[0].sizeClass == TextureSizeClass::Oversized));

    const std::string report = assetReportJson(loaded, results);
    CHECK(report.find("\"schema\": \"zodar.asset_report\"") != std::string::npos);
    CHECK(report.find("\"texelDensity\": 32.0") != std::string::npos);
    CHECK(report.find("\"warnings\": 3") != std::string::npos); // summary: crate, its texture, scene stale
}

TEST_CASE("asset registry: texel density warnings and prefab material directories") {
    const Workspace ws;
    AssetRegistry r(ws.dir);
    importAsset(r, ws.dir / "crate.gltf");
    AssetCheckOptions strict;
    strict.texelDensityTarget = 128.0; // crate: 32 texels/m, outside 64..256
    CHECK(contains(checkAsset(r, r.at("crate"), strict).warnings, "texel density 32.0 texels/m outside 64..256"));

    // Inline materials of prefabs resolve their textures next to the prefab file.
    std::filesystem::create_directories(ws.dir / "props");
    std::filesystem::copy_file(ws.dir / "textures" / "rune.png", ws.dir / "props" / "rune.png");
    ws.write("props/sign.prefab.json", R"({"version": 1, "materials": {"m": {"baseColorTexture": "rune.png"}},
        "entities": [{"id": "s", "meshRenderer": {"mesh": "cube", "material": "m"}}]})");
    ws.write("level.scene.json", R"({"version": 1, "entities": [{"id": "sign", "prefab": "props/sign.prefab.json"}]})");
    const SceneDocument doc = loadSceneFile(ws.dir / "level.scene.json");
    REQUIRE(doc.materials.size() == 1);
    CHECK(sceneMaterialDirectory(doc.materials[0], ws.dir) == (ws.dir / "props").lexically_normal());
    const AssetEntry level = importAsset(r, ws.dir / "level.scene.json");
    CHECK(level.dependencies == std::vector<std::string>{"props/rune.png", "props/sign.prefab.json"});
    CHECK(std::string(checkAsset(r, r.at("level")).status()) == "ok");
}

TEST_CASE("asset registry: the committed example registry is up to date") {
    const AssetRegistry r = AssetRegistry::load(kExample / "assets.json");
    CHECK(r.entries().size() == 5);
    for (const AssetCheckResult& res : checkRegistry(r)) {
        CHECK_MESSAGE(res.errors.empty(), res.id);
        CHECK_MESSAGE(res.warnings.empty(), res.id << " (re-run zodar.py import after editing example assets)");
    }
}
