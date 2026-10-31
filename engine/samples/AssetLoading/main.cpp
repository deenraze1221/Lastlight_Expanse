// Asset loading example (Phase C): a glTF 2.0 crate with a 64x64 PNG texture (nearest glTF
// sampler), plus a scene file whose materials reference PNG textures (pixel-art and linear
// filtering side by side), all loaded asynchronously through zodar::AssetManager (decoding on
// the JobSystem, uploads acquired by the frame loop). Assets appear as they become ready;
// --deterministic waits for them in onStart so every frame is reproducible. Exit code 1
// (fatal error) if an asset fails, is still loading at exit, or outlives its handles.
//   ZodarAssetLoading [--assets DIR] [standard options, see --help]
#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetRegistry.h"
#include "Assets/Primitives.h"
#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Runtime/Application.h"
#include "Runtime/CommandLine.h"
#include "SceneFile/SceneFile.h"
#include "WorldRender/SceneComponents.h"
#include "WorldRender/WorldRender.h"

#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace zodar;
using namespace zodar::gfx;

namespace {

constexpr const char* kUsage = "ZodarAssetLoading [--assets DIR]  (default: the repository's examples/asset_loading)";

class AssetLoadingApp final : public Application {
public:
    AssetLoadingApp(const ApplicationDesc& desc, std::filesystem::path assets)
        : Application(desc), m_assetDir(std::move(assets)) {}

protected:
    void onStart() override {
        AssetManagerDesc desc;
        desc.root = m_assetDir;
        desc.defaultSampler = SamplerDesc::pixelArt(); // pixel-art project default
        desc.jobs = &jobs();
        m_assets = std::make_unique<AssetManager>(renderer().context(), upload(), scene(), desc);

        // The crate is loaded by registry id (assets.json, Phase C3) with its import settings.
        const AssetRegistry registry = AssetRegistry::load(m_assetDir / AssetRegistry::kFileName);
        const AssetEntry& crate = registry.at("crate");
        m_crate = m_assets->loadModelAsync(registry.sourcePath(crate), modelLoadSettings(crate));
        {
            UploadBatch batch(upload());
            m_cube.emplace(batch, primitives::cube(1.0f));
            batch.submit();
        }
        const std::filesystem::path scenePath = m_assets->resolve("asset_loading.scene.json");
        m_document = loadSceneFile(scenePath);
        m_sceneMaterials = m_assets->createSceneMaterialsAsync(m_document, scenePath);
        if (Application::desc().deterministic) {
            m_assets->waitAll();
        }
        trySpawnScene();
    }

    // Instantiates the scene once its materials are ready (they are bound by handle).
    void trySpawnScene() {
        if (m_spawned || m_sceneMaterials.loading()) {
            return;
        }
        if (const auto errors = m_sceneMaterials.errors(); !errors.empty()) {
            fail("scene materials failed: " + errors.front());
        }
        SceneBindings bindings;
        bindings.meshes = {{"cube", &*m_cube}};
        bindings.materials = m_sceneMaterials.handles();
        instantiateScene(m_document, m_world, bindings);
        m_view.clearColor = {};
        collectLighting(m_world, m_view);
        collectCamera(m_world, m_view.camera);
        m_spawned = true;
        logStats("Scene ready");
    }

    void logStats(const char* what) {
        const AssetManager::Stats s = m_assets->stats();
        ZODAR_LOG_INFO("{} (frame {}): {} textures, {} models, {} materials ({} loads, {} cache hits, {} failures, "
                       "{} pending)",
                       what, renderer().frameNumber(), s.textures, s.models, s.materials, s.loads, s.hits, s.failures,
                       s.pending);
    }

    void onUpdate(const FrameTime& time) override {
        m_assets->update();
        trySpawnScene();
        if (m_crate.failed()) {
            fail(m_crate.error());
        }
        m_draws.clear();
        collectDraws(m_world, m_draws);
        const float angle = static_cast<float>(time.time) * 0.6f;
        if (m_crate.ready()) {
            m_crate->model.appendDraws(m_draws, glm::translate(glm::mat4(1.0f), {0.0f, 0.5f, -0.4f}) *
                                                     glm::rotate(glm::mat4(1.0f), angle, {0.0f, 1.0f, 0.0f}));
        }
        m_view.draws = m_draws;
    }

    void onRender(RenderContext& rc) override {
        scene().addToGraph(rc.graph, rc.info, rc.extent, m_view, rc.output, rc.format, &rc.stats);
    }

    void onShutdown() override {
        renderer().waitIdle();
        const bool complete = m_spawned && m_crate.ready() && m_assets->pendingCount() == 0;
        logStats("Shutdown");
        m_view.draws = {};
        m_draws.clear();
        m_world.clear();
        m_sceneMaterials = {};
        m_crate = {};
        renderer().context().collectGarbage();
        const AssetManager::Stats s = m_assets->stats();
        ZODAR_LOG_INFO("Released assets: {} entries; live {} textures, {} models, {} materials", s.released, s.textures,
                       s.models, s.materials);
        m_cube.reset();
        const bool leaked = s.textures + s.models + s.materials != 0;
        m_assets.reset();
        if (!complete) {
            fail("assets still loading at exit (run more frames)");
        }
        if (leaked) {
            fail("assets still alive after their handles were released");
        }
    }

private:
    std::filesystem::path m_assetDir;
    std::unique_ptr<AssetManager> m_assets; // declared first: destroyed after the handles
    ModelHandle m_crate;
    SceneMaterialAssets m_sceneMaterials;
    SceneDocument m_document;
    bool m_spawned = false;
    std::optional<GpuMesh> m_cube;
    World m_world;
    std::vector<MeshDraw> m_draws;
    SceneView m_view;
};

} // namespace

int main(int argc, char** argv) {
    ApplicationDesc defaults;
    defaults.name = "ZODAR Engine - Asset Loading";
    defaults.identifier = "ZodarAssetLoading";
    return runMain(argc, argv, defaults, kUsage, [](const ApplicationDesc& desc, std::span<const std::string> args) {
        std::filesystem::path dir = std::filesystem::path(ZODAR_EXAMPLES_DIR) / "asset_loading";
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--assets" && i + 1 < args.size()) {
                dir = args[++i];
            } else {
                fail("unknown option '" + args[i] + "'");
            }
        }
        return std::make_unique<AssetLoadingApp>(desc, dir);
    });
}
