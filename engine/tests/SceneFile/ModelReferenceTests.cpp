// Phase C4: scene files reference glTF models (modelRenderer) by registry id or file path.
#include "AssetManager/AssetRegistry.h"
#include "SceneFile/SceneFile.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace zodar;

namespace {

const std::filesystem::path kExample = std::filesystem::path(ZODAR_EXAMPLES_DIR) / "asset_loading";

std::vector<DataIssue> issuesOf(std::string_view json, const std::filesystem::path& base = kExample) {
    try {
        (void)parseScene(json, "bad.scene.json", base);
    } catch (const DataError& e) {
        return e.issues();
    }
    return {};
}

bool hasIssue(const std::vector<DataIssue>& issues, std::string_view path, std::string_view text) {
    for (const DataIssue& i : issues) {
        if (i.path == path && i.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Model pointers are only stored and compared by instantiate/capture (never dereferenced).
alignas(gfx::Model) std::byte g_modelA[sizeof(gfx::Model)];
alignas(gfx::Model) std::byte g_modelB[sizeof(gfx::Model)];
const gfx::Model* modelA() { return reinterpret_cast<const gfx::Model*>(g_modelA); }
const gfx::Model* modelB() { return reinterpret_cast<const gfx::Model*>(g_modelB); }

} // namespace

TEST_CASE("model references: file paths vs registry ids") {
    CHECK(isModelFileReference("crate.gltf"));
    CHECK(isModelFileReference("props/Crate.GLB"));
    CHECK_FALSE(isModelFileReference("crate"));
    CHECK_FALSE(isModelFileReference("crate.gltf.zodar"));
}

TEST_CASE("model references: parse, validate and round-trip modelRenderer") {
    const SceneDocument doc = parseScene(R"({"version": 1, "entities": [
        {"id": "a", "modelRenderer": {"model": "crate"}},
        {"id": "b", "modelRenderer": {"model": "crate.gltf", "visible": false}}
    ]})", "s.scene.json", kExample);
    REQUIRE(doc.entities.size() == 2);
    REQUIRE(doc.entities[0].modelRenderer.has_value());
    CHECK(doc.entities[0].modelRenderer->model == "crate");
    CHECK(doc.entities[0].modelRenderer->visible);
    CHECK_FALSE(doc.entities[1].modelRenderer->visible);
    CHECK_FALSE(doc.entities[0].meshRenderer.has_value());
    CHECK(parseScene(serializeScene(doc), "rt", kExample) == doc);

    const auto issues = issuesOf(R"({"version": 1, "materials": {"m": {}}, "entities": [
        {"modelRenderer": {}},
        {"modelRenderer": {"model": "missing.glb", "lod": 1}},
        {"modelRenderer": {"model": "crate"}, "meshRenderer": {"mesh": "cube", "material": "m"}},
        {"modelRenderer": "crate"}
    ]})");
    CHECK(hasIssue(issues, "/entities/0/modelRenderer/model", "required field is missing"));
    CHECK(hasIssue(issues, "/entities/1/modelRenderer/model", "model file 'missing.glb' not found"));
    CHECK(hasIssue(issues, "/entities/1/modelRenderer/lod", "unknown field"));
    CHECK(hasIssue(issues, "/entities/2/modelRenderer", "either a meshRenderer or a modelRenderer"));
    CHECK(hasIssue(issues, "/entities/3/modelRenderer", "expected an object"));
}

TEST_CASE("model references: prefab model paths are rebased; overrides patch modelRenderer") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "zodar_model_ref_tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "props" / "textures");
    std::filesystem::copy_file(kExample / "crate.gltf", dir / "props" / "crate.gltf");
    std::filesystem::copy_file(kExample / "textures" / "crate.png", dir / "props" / "textures" / "crate.png");
    std::ofstream(dir / "props" / "box.prefab.json") << R"({"version": 1, "entities": [
        {"id": "body", "modelRenderer": {"model": "crate.gltf"}}, {"id": "id", "modelRenderer": {"model": "crate"}}]})";
    std::ofstream(dir / "level.scene.json") << R"({"version": 1, "entities": [
        {"id": "box", "prefab": "props/box.prefab.json", "overrides": {"id": {"modelRenderer": {"visible": false}}}}]})";
    const SceneDocument doc = loadSceneFile(dir / "level.scene.json");
    REQUIRE(doc.entities.size() == 3);
    CHECK(doc.entities[1].modelRenderer->model == "props/crate.gltf"); // rebased path
    CHECK(doc.entities[2].modelRenderer->model == "crate");            // ids stay
    CHECK_FALSE(doc.entities[2].modelRenderer->visible);

    // The asset registry hashes path-referenced models (and their textures); ids are checked.
    AssetRegistry r(dir);
    const AssetEntry level = importAsset(r, dir / "level.scene.json");
    CHECK(level.dependencies ==
          std::vector<std::string>{"props/box.prefab.json", "props/crate.gltf", "props/textures/crate.png"});
    AssetCheckResult check = checkAsset(r, r.at("level"));
    REQUIRE(check.errors.size() == 1);
    CHECK(check.errors[0] == "modelRenderer: unknown asset id 'crate'");
    importAsset(r, dir / "props" / "textures" / "crate.png");
    check = checkAsset(r, r.at("level"));
    CHECK(check.errors == std::vector<std::string>{"modelRenderer: asset 'crate' is a texture, not a model"});
    AssetImportOptions asTexture;
    asTexture.id = "crate_texture";
    r.remove("crate");
    importAsset(r, dir / "props" / "textures" / "crate.png", asTexture);
    importAsset(r, dir / "props" / "crate.gltf");
    CHECK(std::string(checkAsset(r, r.at("level")).status()) == "ok");
    std::filesystem::remove_all(dir);
}

TEST_CASE("model references: instantiate binds ModelRenderer; capture writes it back") {
    const SceneDocument doc = parseScene(R"({"version": 1, "entities": [
        {"id": "a", "name": "A", "modelRenderer": {"model": "crate"}},
        {"name": "B", "parent": "a", "modelRenderer": {"model": "crate.gltf", "visible": false}}
    ]})", "s.scene.json", kExample);
    World world;
    SceneBindings bindings;
    bindings.models = {{"crate", modelA()}};
    CHECK_THROWS_WITH_AS(instantiateScene(doc, world, bindings), doctest::Contains("model 'crate.gltf' is not bound"),
                         DataError);
    CHECK(world.entityCount() == 0); // unchanged on error
    bindings.models["crate.gltf"] = modelB();
    const SceneInstance instance = instantiateScene(doc, world, bindings);
    const ModelRenderer& a = world.get<ModelRenderer>(instance.entities[0]);
    CHECK(a.model == modelA());
    CHECK(a.visible);
    CHECK_FALSE(world.get<ModelRenderer>(instance.entities[1]).visible);

    const SceneDocument captured = captureScene(world, invertBindings(doc, bindings));
    REQUIRE(captured.entities.size() == 2);
    CHECK(captured.entities[0].modelRenderer == SceneModelRenderer{"crate", true});
    CHECK(captured.entities[1].modelRenderer == SceneModelRenderer{"crate.gltf", false});
    SceneCaptureNames noModels = invertBindings(doc, bindings);
    noModels.models.clear();
    CHECK_THROWS_WITH_AS((void)captureScene(world, noModels), doctest::Contains("model has no reference"), DataError);
}
