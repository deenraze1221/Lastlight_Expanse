#include "AssetManager/LiveScene.h"

#include "Core/Error.h"
#include "Core/Log.h"

#include <exception>
#include <utility>

namespace zodar {

LiveScene::LiveScene(AssetManager& assets, World& world, std::filesystem::path sceneFile, LiveSceneDesc desc)
    : m_assets(assets), m_world(world), m_path(assets.resolve(sceneFile)), m_desc(std::move(desc)) {}

LiveScene::~LiveScene() { destroyEntities(m_instance); }

void LiveScene::destroyEntities(const SceneInstance& instance) {
    for (const Entity e : instance.entities) {
        if (m_world.isAlive(e)) {
            m_world.destroy(e); // removes the subtree too
        }
    }
}

bool LiveScene::reload() {
    std::vector<std::filesystem::path> files;
    SceneDocument document;
    SceneModelAssets models;
    SceneMaterialAssets materials;
    SceneInstance instance;
    bool instantiated = false;
    try {
        document = loadSceneFile(m_path, &files);
        models = m_assets.loadSceneModels(document, m_path, m_desc.registry);
        materials = m_assets.createSceneMaterials(document, m_path);

        // Cached inline materials keep their old data: collect the changed ones.
        std::vector<MaterialDataUpdate> updates;
        const std::filesystem::path directory = m_path.parent_path();
        for (const SceneMaterial& material : document.materials) {
            const MaterialAssetHandle& handle = materials.byId.at(material.id);
            if (material.source.empty() && !(handle->data == material.data)) {
                updates.push_back({handle, material.data, sceneMaterialDirectory(material, directory)});
            }
        }

        SceneBindings bindings;
        bindings.meshes = m_desc.meshes;
        bindings.materials = materials.handles();
        bindings.models = models.models();
        // Validated first: on DataError the world is unchanged.
        instance = instantiateScene(document, m_world, bindings, m_path.generic_string());
        instantiated = true;
        if (!updates.empty()) {
            const AssetReloadResult r = m_assets.updateMaterialData(updates); // all or nothing
            if (!r.ok()) {
                std::string message = "inline material update failed:";
                for (const std::string& f : r.failed) {
                    message += "\n  " + f;
                }
                fail(message);
            }
        }
        m_bindings = std::move(bindings);
    } catch (const std::exception& e) {
        if (instantiated) {
            destroyEntities(instance);
        }
        if (!files.empty()) {
            m_files = std::move(files); // watch the files involved, e.g. a broken prefab
        }
        m_error = e.what();
        ZODAR_LOG_WARN("Scene '{}' {} failed{}: {}", m_path.generic_string(), loaded() ? "reload" : "load",
                       loaded() ? " (previous version kept)" : "", m_error);
        return false;
    }
    destroyEntities(m_instance);
    m_files = std::move(files);
    m_document = std::move(document);
    m_instance = std::move(instance);
    m_materials = std::move(materials); // old handles dropped: unused assets are retired
    m_models = std::move(models);
    m_error.clear();
    ++m_generation;
    if (m_generation > 1) {
        ZODAR_LOG_INFO("Hot reload: scene '{}' reloaded ({} entities)", m_path.generic_string(),
                       m_instance.entities.size());
    }
    return true;
}

bool LiveScene::update() {
    if (!watching() || !m_watcher.due(m_desc.watchInterval)) {
        return false;
    }
    std::vector<std::filesystem::path> files = m_files;
    files.push_back(m_path); // also before the first successful load
    m_watcher.setWatched(files);
    if (m_watcher.poll().empty()) {
        return false;
    }
    return reload();
}

} // namespace zodar
