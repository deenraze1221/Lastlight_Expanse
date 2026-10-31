#pragma once

#include "SceneFile/DataError.h"
#include "SceneFile/MaterialFile.h"
#include "World/Components.h"
#include "World/World.h"
#include "WorldRender/WorldRender.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Scene files (*.scene.json, Phase B2). Schema and examples: docs/guide/SCENE_FILES.md,
// examples/scene_from_json. Flow:
//   SceneDocument doc = loadSceneFile("level.scene.json");        // parse + validate (CPU)
//   SceneBindings b;  b.meshes["cube"] = &cubeMesh;                 // meshes come from code
//   b.materials = createSceneMaterials(doc, renderer.materials());  // GPU materials
//   SceneInstance s = instantiateScene(doc, world, b);              // entities in the World
// Rendering then goes through the existing WorldRender bridge (collectDraws / WorldRenderSync).
namespace zodar {

// A material defined by the scene: inline, or loaded from `source` (path as written in the
// file, relative to the scene file's directory). `data` is filled in both cases.
struct SceneMaterial {
    std::string id;
    std::string source;
    MaterialData data;

    bool operator==(const SceneMaterial&) const = default;
};

// MeshRenderer by name: `mesh` is resolved through SceneBindings (meshes are provided by code
// until the Phase C asset manager), `material` names a SceneMaterial id.
struct SceneMeshRenderer {
    std::string mesh;
    uint32_t submesh = 0;
    std::string material;
    bool visible = true;

    bool operator==(const SceneMeshRenderer&) const = default;
};

struct SceneEntity {
    std::string id;     // Optional, unique; needed only to be referenced as a parent.
    std::string parent; // Id of the parent entity; empty for roots.
    std::optional<std::string> name;
    std::optional<Transform> transform;
    std::optional<SceneMeshRenderer> meshRenderer;

    bool operator==(const SceneEntity&) const = default;
};

struct SceneDocument {
    static constexpr uint32_t kVersion = 1;

    std::vector<SceneMaterial> materials; // File order.
    std::vector<SceneEntity> entities;    // File order (= creation and child order).

    bool operator==(const SceneDocument&) const = default;
};

// Parses and validates scene JSON. Material references (string values in "materials") are
// loaded relative to `baseDirectory`. `file` names the source in errors. Throws DataError
// listing every problem found (syntax: file:line:column; schema: JSON pointer path).
[[nodiscard]] SceneDocument parseScene(std::string_view json, std::string_view file = "<memory>",
                                       const std::filesystem::path& baseDirectory = {});

// Reads and parses a scene file; material references resolve next to it.
[[nodiscard]] SceneDocument loadSceneFile(const std::filesystem::path& path);

// Deterministic, pretty-printed JSON. Referenced materials are written as their `source`
// path (the material file itself is not written), inline ones in full.
// parseScene(serializeScene(doc), ..., same base directory) == doc.
[[nodiscard]] std::string serializeScene(const SceneDocument& document);

// Engine objects the scene's names resolve to.
struct SceneBindings {
    std::unordered_map<std::string, const gfx::GpuMesh*> meshes;
    std::unordered_map<std::string, gfx::MaterialHandle> materials;
};

// Creates every scene material in `materials` (file order); result maps material id ->
// handle. Throws DataError for unknown shader names before creating anything.
[[nodiscard]] std::unordered_map<std::string, gfx::MaterialHandle>
createSceneMaterials(const SceneDocument& document, gfx::MaterialSystem& materials, std::string_view file = "<scene>");

struct SceneInstance {
    std::vector<Entity> entities;                 // Same order as SceneDocument::entities.
    std::unordered_map<std::string, Entity> byId; // Entities that have an id.
};

// Creates the document's entities in `world` (Name, Transform, MeshRenderer, hierarchy).
// All mesh / material names and submesh indices are checked first: on DataError the world is
// unchanged. Bound meshes must outlive the entities that use them.
SceneInstance instantiateScene(const SceneDocument& document, World& world, const SceneBindings& bindings,
                               std::string_view file = "<scene>");

} // namespace zodar
