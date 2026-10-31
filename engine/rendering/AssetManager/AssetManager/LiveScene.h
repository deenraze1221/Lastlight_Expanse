#pragma once

#include "AssetManager/AssetManager.h"
#include "Core/FileWatcher.h"
#include "SceneFile/SceneFile.h"
#include "World/World.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// A scene file kept in sync with a World (Phase C6, scene hot reload).
//
//   LiveSceneDesc desc;
//   desc.meshes["cube"] = &cubeMesh;           // meshRenderer meshes come from code
//   desc.watch = true;                          // Debug builds: reload when the file changes
//   LiveScene level(assets, world, "levels/a.scene.json", desc);
//   if (!level.load()) log(level.error());      // world unchanged on failure
//   ... every frame: level.update();            // polls the scene + prefab files
//   Entity door = level.instance().byId.at("door");
//
// load()/reload() parse the file (prefabs expanded), load its materials and models through
// the AssetManager, instantiate the entities and, only when everything succeeded, destroy the
// previously instantiated entities (with their subtrees) and drop the old asset handles. On any
// failure (syntax/schema error, missing prefab, material, texture, model or mesh binding) the
// current entities and assets stay as they are and error() says why.
// Inline materials whose data changed are updated in place (AssetManager::updateMaterialData,
// same gfx handles); materials in their own files and textures/models are reloaded by the
// AssetManager (its own watchFiles). Shader, alphaMode or doubleSided changes of an inline
// material fail the reload (pipeline state).
// After a reload the scene's entities are new: re-read instance()/bindings() (generation()
// changes); components or children added by code to scene entities are lost with them.
// File watching (desc.watch) only works when kHotReloadAvailable (Debug builds); reload()
// works everywhere. Rules: render thread; `assets` and `world` outlive the LiveScene, whose
// destructor destroys its entities.
namespace zodar {

class AssetRegistry;

struct LiveSceneDesc {
    const AssetRegistry* registry = nullptr; // modelRenderer ids (AssetManager::loadSceneModels)
    std::unordered_map<std::string, const gfx::GpuMesh*> meshes; // meshRenderer meshes by name
    bool watch = false; // reload in update() when the scene or one of its prefabs changes
    std::chrono::milliseconds watchInterval{250};
};

class LiveScene {
public:
    // A relative `sceneFile` resolves against the AssetManager root (AssetManager::resolve).
    LiveScene(AssetManager& assets, World& world, std::filesystem::path sceneFile, LiveSceneDesc desc = {});
    ~LiveScene();

    LiveScene(const LiveScene&) = delete;
    LiveScene& operator=(const LiveScene&) = delete;

    // (Re)loads the scene file now. Returns false (error() set, previous state kept) on failure.
    bool load() { return reload(); }
    bool reload();
    // While watching: checks the scene and prefab files every watchInterval and reloads after
    // a change. Returns true if a reload succeeded in this call.
    bool update();
    bool watching() const { return kHotReloadAvailable && m_desc.watch; }

    bool loaded() const { return m_generation > 0; }
    uint64_t generation() const { return m_generation; } // successful loads
    const std::string& error() const { return m_error; } // last failure; empty after a success
    const std::filesystem::path& path() const { return m_path; }
    // Scene file + prefab files of the last attempt (absolute), as watched.
    const std::vector<std::filesystem::path>& files() const { return m_files; }

    const SceneDocument& document() const { return m_document; }
    const SceneInstance& instance() const { return m_instance; }
    const SceneBindings& bindings() const { return m_bindings; }
    const SceneMaterialAssets& materials() const { return m_materials; }
    const SceneModelAssets& models() const { return m_models; }

private:
    void destroyEntities(const SceneInstance& instance);

    AssetManager& m_assets;
    World& m_world;
    std::filesystem::path m_path;
    LiveSceneDesc m_desc;
    FileWatcher m_watcher;
    std::vector<std::filesystem::path> m_files;
    uint64_t m_generation = 0;
    std::string m_error;

    SceneDocument m_document;
    SceneInstance m_instance;
    SceneBindings m_bindings;
    SceneMaterialAssets m_materials;
    SceneModelAssets m_models;
};

} // namespace zodar
