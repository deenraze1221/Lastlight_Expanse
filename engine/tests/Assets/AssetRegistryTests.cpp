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

TEST_CASE("asset registry: hardening (sources outside the directory, ids, URI decoding, atomic save)") {
    const Workspace w;
    // Sources must stay inside the registry directory.
    const auto escaping = parseIssues(
        R"({"schema": "zodar.asset_registry", "version": 1, "assets": [{"id": "a", "type": "texture", "source": "../x.png"}]})");
    REQUIRE_FALSE(escaping.empty());
    CHECK(escaping[0].message.find("inside the registry directory") != std::string::npos);
    CHECK_FALSE(parseIssues(R"({"schema": "zodar.asset_registry", "version": 1, "assets": [{"id": "a", "type": "texture", "source": "/abs/x.png"}]})")
                    .empty());

    AssetRegistry r(w.dir);
    // A file name starting with ".." is inside the directory.
    std::filesystem::copy(w.dir / "textures" / "crate.png", w.dir / "..hero.png");
    CHECK(importAsset(r, w.dir / "..hero.png").source == "..hero.png");
    CHECK_THROWS_WITH_AS(importAsset(r, w.dir.parent_path() / "outside.png"), doctest::Contains("file not found"), Error);

    // --id: one source has one entry; settings of another file never carry over.
    importAsset(r, w.dir / "textures" / "crate.png");
    AssetImportOptions other;
    other.id = "other";
    CHECK_THROWS_WITH_AS(importAsset(r, w.dir / "textures" / "crate.png", other), doctest::Contains("already imported as"),
                         Error);
    AssetImportOptions normalMap;
    normalMap.usage = TextureUsage::Normal;
    importAsset(r, w.dir / "textures" / "rune.png", normalMap);
    AssetImportOptions retarget;
    retarget.id = "rune";
    std::filesystem::copy(w.dir / "textures" / "crate.png", w.dir / "fresh.png");
    const AssetEntry moved = importAsset(r, w.dir / "fresh.png", retarget);
    CHECK(moved.source == "fresh.png");
    CHECK((moved.usage == TextureUsage::Color)); // not the old entry's normal-map usage

    // glTF URIs: %XX decoded, malformed escapes kept literally (no exception, no wrong file).
    w.write("uris.gltf", R"({"asset": {"version": "2.0"}, "buffers": [{"uri": "a%20b.bin", "byteLength": 1},
        {"uri": "c%zzd.bin", "byteLength": 1}, {"uri": "e%1gf.bin", "byteLength": 1}]})");
    const std::vector<std::filesystem::path> deps = assetDependencies(AssetType::Model, w.dir / "uris.gltf");
    std::vector<std::string> names;
    for (const auto& d : deps) {
        names.push_back(d.filename().string());
    }
    CHECK(names == std::vector<std::string>{"a b.bin", "c%zzd.bin", "e%1gf.bin"});

    // save() replaces the file atomically and leaves no temporary file.
    r.save(w.dir / "assets.json");
    CHECK(AssetRegistry::load(w.dir / "assets.json").serialize() == r.serialize());
    CHECK_FALSE(std::filesystem::exists(w.dir / "assets.json.tmp"));
    CHECK_THROWS_AS(r.save(w.dir / "missing_dir" / "assets.json"), Error);
}

// --- Phase V2: configurable texture standards ------------------------------------------------

namespace {

const char* kStandardRegistry = R"({"schema": "zodar.asset_registry", "version": 1,
    "textureStandards": {"classes": {"environment": [64], "small": [32], "character": [128]},
                         "defaultClass": "environment", "densityTarget": 64},
    "defaults": {"texture": {"textureClass": "small"}, "model": {"textureClass": "environment"}},
    "assets": []})";

} // namespace

TEST_CASE("asset registry: textureStandards parse, validation paths and round-trip") {
    const AssetRegistry r = AssetRegistry::parse(kStandardRegistry, "reg/assets.json");
    REQUIRE(r.textureStandards().has_value());
    const TextureStandard& s = *r.textureStandards();
    REQUIRE(s.classes.size() == 3);
    CHECK(s.classes[0].name == "character"); // sorted by name
    CHECK(s.classes[1].name == "environment");
    CHECK(s.classes[1].sizes == std::vector<uint32_t>{64});
    CHECK(s.defaultClass == "environment");
    CHECK(s.prohibitedSize == 256);
    CHECK(s.densityTarget == 64.0);
    CHECK(s.densityTolerance == 2.0);
    CHECK_FALSE(s.engineRule);
    CHECK(r.defaults().at(AssetType::Texture).textureClass == std::optional<std::string>("small"));
    CHECK(r.textureStandard() == s);
    const AssetRegistry back = AssetRegistry::parse(r.serialize(), "reg/assets.json");
    CHECK(back == r);
    CHECK(back.serialize() == r.serialize());
    CHECK(r.serialize().find("\"textureStandards\"") != std::string::npos);

    // Without the key nothing is written and the engine default applies.
    const AssetRegistry plain("root");
    CHECK_FALSE(plain.textureStandards().has_value());
    CHECK(plain.textureStandard() == TextureStandard::engineDefault());
    CHECK(plain.serialize().find("textureStandards") == std::string::npos);

    // setTextureStandards validates and sorts the classes (parse == set).
    AssetRegistry set("reg");
    TextureStandard unsorted = s;
    std::swap(unsorted.classes[0], unsorted.classes[2]);
    set.setTextureStandards(unsorted);
    CHECK(set.textureStandards() == r.textureStandards());
    TextureStandard broken = s;
    broken.defaultClass = "nope";
    CHECK_THROWS_AS(set.setTextureStandards(broken), Error);

    const auto issues = parseIssues(R"({"schema": "zodar.asset_registry", "version": 1,
        "textureStandards": {"classes": {"env": [64, 48], "big": [512], "down": [64, 32], "empty": []},
                             "defaultClass": "env", "prohibitedSize": 300, "densityTarget": -1, "extra": 1},
        "assets": [
            {"id": "t", "type": "texture", "source": "t.png", "textureClass": "env"},
            {"id": "s", "type": "scene", "source": "s.scene.json", "textureClass": "env"}
        ]})");
    const auto has = [&](std::string_view path, std::string_view text) {
        for (const DataIssue& i : issues) {
            if (i.path == path && i.message.find(text) != std::string::npos) return true;
        }
        return false;
    };
    CHECK(has("/textureStandards/prohibitedSize", "must be in 1..256"));
    CHECK(has("/textureStandards/classes/env/1", "size 48 is not a power of two"));
    CHECK(has("/textureStandards/classes/big/0", "at or above the prohibited size 300"));
    CHECK(has("/textureStandards/classes/down/1", "ascending and unique"));
    CHECK(has("/textureStandards/classes/empty", "non-empty array"));
    CHECK(has("/textureStandards/densityTarget", "positive number"));
    CHECK(has("/textureStandards/extra", "unknown field"));
    CHECK(has("/assets/1/textureClass", "unknown field")); // scenes take no texture class

    // An invalid standard is reported once; classes referring to it are not reported again.
    const auto badDefault = parseIssues(R"({"schema": "zodar.asset_registry", "version": 1,
        "textureStandards": {"classes": {"env": [64]}, "defaultClass": "nope"},
        "assets": [{"id": "m", "type": "model", "source": "m.glb", "textureClass": "props"}]})");
    REQUIRE(badDefault.size() == 1);
    CHECK(badDefault[0].path == "/textureStandards/defaultClass");
    const auto classIssues = parseIssues(R"({"schema": "zodar.asset_registry", "version": 1,
        "textureStandards": {"classes": {"env": [64]}, "defaultClass": "env"},
        "defaults": {"model": {"textureClass": "props"}},
        "assets": [{"id": "m", "type": "model", "source": "m.glb", "textureClass": "props"}]})");
    REQUIRE(classIssues.size() == 2);
    CHECK(classIssues[0].path == "/defaults/model/textureClass");
    CHECK(classIssues[0].message.find("unknown texture class 'props' (classes: env)") != std::string::npos);
    CHECK(classIssues[1].path == "/assets/0/textureClass");
    const auto noStandard = parseIssues(R"({"schema": "zodar.asset_registry", "version": 1,
        "assets": [{"id": "t", "type": "texture", "source": "t.png", "textureClass": "env"}]})");
    REQUIRE(noStandard.size() == 1);
    CHECK(noStandard[0].path == "/assets/0/textureClass");
    CHECK(noStandard[0].message.find("the registry has no \"textureStandards\"") != std::string::npos);
}

TEST_CASE("asset registry: texture sizes are checked against the entry's texture class") {
    const Workspace ws;
    ws.write("assets.json", kStandardRegistry);
    AssetRegistry r = AssetRegistry::load(ws.dir / "assets.json");
    const auto png = [&](const char* name, uint32_t w, uint32_t h) {
        REQUIRE(writePng(ws.dir / "textures" / name, w, h, std::vector<uint8_t>(size_t(w) * h * 4, 90)));
    };
    png("tile64.png", 64, 64);
    png("tile32.png", 32, 32);
    png("hero128.png", 128, 128);
    png("huge256.png", 256, 256);
    png("wide.png", 64, 32);

    // Defaults: new textures get "small" (32 only).
    importAsset(r, ws.dir / "textures" / "tile32.png");
    CHECK(r.at("tile32").textureClass == "small");
    CHECK(std::string(checkAsset(r, r.at("tile32")).status()) == "ok");
    AssetImportOptions env;
    env.textureClass = "environment";
    importAsset(r, ws.dir / "textures" / "tile64.png", env);
    CHECK(std::string(checkAsset(r, r.at("tile64")).status()) == "ok");
    // 64x64 as "small": a warning naming the class (fails --strict), not an error.
    AssetImportOptions small;
    small.textureClass = "small";
    small.id = "tile64_small";
    REQUIRE(std::filesystem::copy_file(ws.dir / "textures" / "tile64.png", ws.dir / "textures" / "tile64b.png"));
    importAsset(r, ws.dir / "textures" / "tile64b.png", small);
    const AssetCheckResult wrong = checkAsset(r, r.at("tile64_small"));
    CHECK(wrong.errors.empty());
    CHECK(contains(wrong.warnings, "64x64 is not allowed for texture class 'small' (32x32;"));
    CHECK(wrong.textureClass == "small");
    // 128 only for characters.
    AssetImportOptions character;
    character.textureClass = "character";
    importAsset(r, ws.dir / "textures" / "hero128.png", character);
    CHECK(std::string(checkAsset(r, r.at("hero128")).status()) == "ok");
    // 256: an error whatever the class.
    importAsset(r, ws.dir / "textures" / "huge256.png", character);
    const AssetCheckResult huge = checkAsset(r, r.at("huge256"));
    CHECK(contains(huge.errors, "huge256.png: 256x256 is prohibited"));
    CHECK(huge.warnings.empty());
    importAsset(r, ws.dir / "textures" / "wide.png", env);
    CHECK(contains(checkAsset(r, r.at("wide")).warnings, "64x32 is not allowed for texture class 'environment'"));
    // Unknown classes are rejected at import; "" clears the class (default class applies).
    AssetImportOptions unknown;
    unknown.textureClass = "vehicle";
    CHECK_THROWS_AS(importAsset(r, ws.dir / "textures" / "tile64.png", unknown), Error);
    AssetImportOptions clear;
    clear.textureClass = "";
    importAsset(r, ws.dir / "textures" / "tile32.png", clear);
    CHECK(r.at("tile32").textureClass.empty());
    CHECK(contains(checkAsset(r, r.at("tile32")).warnings, "not allowed for texture class 'environment'"));
    AssetImportOptions sceneClass;
    sceneClass.textureClass = "small";
    CHECK_THROWS_AS(importAsset(r, ws.dir / "asset_loading.scene.json", sceneClass), Error);

    // Models: embedded/external images use the model's texture class; density uses the
    // registry target (64) unless the options override it.
    importAsset(r, ws.dir / "crate.gltf");
    CHECK(r.at("crate").textureClass == "environment"); // model default
    const AssetCheckResult crate = checkAsset(r, r.at("crate"));
    CHECK(contains(crate.warnings, "32x32 is not allowed for texture class 'environment'"));
    CHECK(contains(crate.warnings, "texel density 32.0 texels/m outside 32..128 (target 64)") == false);
    AssetCheckOptions dense;
    dense.texelDensityTarget = 128.0;
    CHECK(contains(checkAsset(r, r.at("crate"), dense).warnings, "texel density 32.0 texels/m outside 64..256"));
    AssetCheckOptions tight;
    tight.texelDensityTolerance = 1.5;
    CHECK(contains(checkAsset(r, r.at("crate"), tight).warnings, "texel density 32.0 texels/m outside 43..96 (target 64)"));

    // Round trip keeps texture classes; the report names them.
    r.save(ws.dir / "assets.json");
    const AssetRegistry loaded = AssetRegistry::load(ws.dir / "assets.json");
    CHECK(loaded == r);
    const std::string report = assetReportJson(loaded, checkRegistry(loaded));
    CHECK(report.find("\"textureClass\": \"character\"") != std::string::npos);
    CHECK(report.find("is prohibited") != std::string::npos);
}

TEST_CASE("asset registry: 256 and above are errors under the engine default too") {
    const Workspace ws;
    AssetRegistry r(ws.dir);
    REQUIRE(writePng(ws.dir / "textures" / "huge.png", 32, 256, std::vector<uint8_t>(32 * 256 * 4, 9)));
    importAsset(r, ws.dir / "textures" / "huge.png");
    const AssetCheckResult res = checkAsset(r, r.at("huge"));
    CHECK(contains(res.errors, "huge.png: 32x256 is prohibited (256x256 and above need an approved exception"));
    CHECK(res.warnings.empty());
    AssetCheckOptions off;
    off.checkTextureSizes = false;
    CHECK(checkAsset(r, r.at("huge"), off).errors.empty());
    // 128x128 stays a warning under the engine default (unchanged).
    REQUIRE(writePng(ws.dir / "textures" / "big.png", 128, 128, std::vector<uint8_t>(128 * 128 * 4, 9)));
    importAsset(r, ws.dir / "textures" / "big.png");
    CHECK(checkAsset(r, r.at("big")).errors.empty());
}
