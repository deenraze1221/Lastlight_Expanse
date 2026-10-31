#pragma once

#include "SceneFile/DataError.h"
#include "SceneFile/MaterialFile.h"
#include "World/Components.h"
#include "World/World.h"
#include "WorldRender/SceneComponents.h"
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
// Rendering then goes through the existing WorldRender bridge (collectDraws / WorldRenderSync,
// collectLighting / collectCamera). captureScene saves a live World back to a document.
//
// Prefabs (*.prefab.json, same format as a scene) are expanded at parse time: an entity with
// "prefab": "<path>" becomes the parent of the prefab's root entities; prefab entity ids become
// "<instance id>/<id>", prefab material ids "<prefab path>#<id>"; "overrides" patches fields of
// prefab entities by id. The resulting document is flat (serializeScene writes no prefab refs).
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

// ModelRenderer (Phase C4): renders a whole glTF model. `model` is either a file path (ends in
// .gltf or .glb, any case; relative to the scene file, prefab paths are rebased like material
// files) or an asset registry id (anything else; assets.json). Resolved through
// SceneBindings::models; AssetManager::loadSceneModels loads every reference.
struct SceneModelRenderer {
    std::string model;
    bool visible = true;

    bool operator==(const SceneModelRenderer&) const = default;
};

// True if a modelRenderer reference is a file path (.gltf / .glb), false for a registry id.
[[nodiscard]] bool isModelFileReference(std::string_view reference);

struct SceneEntity {
    std::string id;     // Optional, unique; needed only to be referenced as a parent.
    std::string parent; // Id of the parent entity; empty for roots.
    std::optional<std::string> name;
    std::optional<Transform> transform;
    std::optional<SceneMeshRenderer> meshRenderer;
    std::optional<SceneModelRenderer> modelRenderer; // exclusive with meshRenderer
    // Reflected components (WorldRender/SceneComponents.h); JSON keys = ComponentReflection::key.
    std::optional<CameraComponent> camera;
    std::optional<DirectionalLightComponent> directionalLight;
    std::optional<PointLightComponent> pointLight;
    std::optional<SpotLightComponent> spotLight;
    std::optional<EnvironmentComponent> environment;
    std::optional<FogComponent> fog;

    bool operator==(const SceneEntity&) const = default;
};

struct SceneDocument {
    static constexpr uint32_t kVersion = 1;

    std::vector<SceneMaterial> materials; // File order.
    std::vector<SceneEntity> entities;    // File order (= creation and child order).

    bool operator==(const SceneDocument&) const = default;
};

// Parses and validates scene JSON (prefabs expanded). Material references (string values in "materials") are
// loaded relative to `baseDirectory`. `file` names the source in errors. Throws DataError
// listing every problem found (syntax: file:line:column; schema: JSON pointer path).
[[nodiscard]] SceneDocument parseScene(std::string_view json, std::string_view file = "<memory>",
                                       const std::filesystem::path& baseDirectory = {});

// Reads and parses a scene (or prefab) file; material and prefab references resolve next to it.
[[nodiscard]] SceneDocument loadSceneFile(const std::filesystem::path& path);

// Directory that relative paths inside `material` (texture slots of inline materials) resolve
// against: the scene's directory, or the defining prefab's directory for prefab materials
// (id "<prefab path>#<id>", nested prefabs chained). File-referenced materials resolve their
// textures next to `sceneDirectory / material.source` instead.
[[nodiscard]] std::filesystem::path sceneMaterialDirectory(const SceneMaterial& material,
                                                           const std::filesystem::path& sceneDirectory);

// Deterministic, pretty-printed JSON. Referenced materials are written as their `source`
// path (the material file itself is not written), inline ones in full.
// parseScene(serializeScene(doc), ..., same base directory) == doc.
[[nodiscard]] std::string serializeScene(const SceneDocument& document);

// Engine objects the scene's names resolve to.
struct SceneBindings {
    std::unordered_map<std::string, const gfx::GpuMesh*> meshes;
    std::unordered_map<std::string, gfx::MaterialHandle> materials;
    // modelRenderer references (as written in the document) -> models (Phase C4).
    std::unordered_map<std::string, const gfx::Model*> models;
};

// Creates every scene material in `materials` (file order); result maps material id ->
// handle. Throws DataError for unknown shader names before creating anything. Texture slots
// are not loaded here (neutral defaults): use AssetManager::createSceneMaterials for textures.
[[nodiscard]] std::unordered_map<std::string, gfx::MaterialHandle>
createSceneMaterials(const SceneDocument& document, gfx::MaterialSystem& materials, std::string_view file = "<scene>");

struct SceneInstance {
    std::vector<Entity> entities;                 // Same order as SceneDocument::entities.
    std::unordered_map<std::string, Entity> byId; // Entities that have an id.
};

// Creates the document's entities in `world` (Name, Transform, MeshRenderer, ModelRenderer, reflected
// components, hierarchy). Error paths use document indices ("/entities/<i>").
// All mesh / material / model names and submesh indices are checked first: on DataError the
// world is unchanged. Bound meshes and models must outlive the entities that use them.
SceneInstance instantiateScene(const SceneDocument& document, World& world, const SceneBindings& bindings,
                               std::string_view file = "<scene>");

// Names used to write a live World back into a document. invertBindings() builds them from
// the document and bindings a scene was instantiated with.
struct SceneCaptureNames {
    std::unordered_map<const gfx::GpuMesh*, std::string> meshes;
    std::unordered_map<gfx::MaterialHandle, std::string> materials; // Handle -> material id.
    std::unordered_map<const gfx::Model*, std::string> models;      // Model -> reference.
    std::vector<SceneMaterial> materialDefinitions;                 // Written to "materials".
};
[[nodiscard]] SceneCaptureNames invertBindings(const SceneDocument& document, const SceneBindings& bindings);

// Writes every live entity of `world` (roots by entity index, then children depth-first in
// child order, so instantiating the result reproduces the hierarchy). Entities with children
// get generated ids "e<n>" (n = output index). Throws DataError naming the entity for a mesh
// or material missing from `names`.
[[nodiscard]] SceneDocument captureScene(const World& world, const SceneCaptureNames& names,
                                         std::string_view file = "<world>");

// Markdown reference of every scene-file component, generated from the reflection tables
// (docs/guide/SCENE_COMPONENTS.md is this text; a unit test keeps them in sync).
[[nodiscard]] std::string sceneComponentReference();

} // namespace zodar
