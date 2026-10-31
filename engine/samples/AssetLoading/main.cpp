// Asset loading example (Phase C): a glTF 2.0 crate with a 64x64 PNG texture (nearest glTF
// sampler), plus a scene file whose materials reference PNG textures (pixel-art and linear
// filtering side by side), all loaded through zodar::AssetManager. Exit code 1 (fatal error)
// if an asset fails to load or is still alive after its handles were released.
//   ZodarAssetLoading [--assets DIR] [standard options, see --help]
#include "AssetManager/AssetManager.h"
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
        m_assets = std::make_unique<AssetManager>(renderer().context(), upload(), scene(), desc);

        m_crate = m_assets->loadModel("crate.gltf");
        if (m_crate.failed()) {
            fail(m_crate.error());
        }
        {
            UploadBatch batch(upload());
            m_cube.emplace(batch, primitives::cube(1.0f));
            batch.submit();
        }
        const std::filesystem::path scenePath = m_assets->resolve("asset_loading.scene.json");
        const SceneDocument doc = loadSceneFile(scenePath);
        m_sceneMaterials = m_assets->createSceneMaterials(doc, scenePath);
        SceneBindings bindings;
        bindings.meshes = {{"cube", &*m_cube}};
        bindings.materials = m_sceneMaterials.handles();
        instantiateScene(doc, m_world, bindings);

        m_view.clearColor = {};
        collectLighting(m_world, m_view);
        collectCamera(m_world, m_view.camera);
        const AssetManager::Stats s = m_assets->stats();
        ZODAR_LOG_INFO("Assets: {} textures, {} models, {} materials ({} loads, {} cache hits, {} failures)", s.textures,
                       s.models, s.materials, s.loads, s.hits, s.failures);
        if (s.failures != 0) {
            fail("asset loading failed");
        }
    }

    void onUpdate(const FrameTime& time) override {
        m_draws.clear();
        collectDraws(m_world, m_draws);
        const float angle = static_cast<float>(time.time) * 0.6f;
        m_crate->model.appendDraws(m_draws, glm::translate(glm::mat4(1.0f), {0.0f, 0.5f, -0.4f}) *
                                                     glm::rotate(glm::mat4(1.0f), angle, {0.0f, 1.0f, 0.0f}));
        m_view.draws = m_draws;
    }

    void onRender(RenderContext& rc) override {
        scene().addToGraph(rc.graph, rc.info, rc.extent, m_view, rc.output, rc.format, &rc.stats);
    }

    void onShutdown() override {
        renderer().waitIdle();
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
        if (leaked) {
            fail("assets still alive after their handles were released");
        }
    }

private:
    std::filesystem::path m_assetDir;
    std::unique_ptr<AssetManager> m_assets; // declared first: destroyed after the handles
    ModelHandle m_crate;
    SceneMaterialAssets m_sceneMaterials;
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
