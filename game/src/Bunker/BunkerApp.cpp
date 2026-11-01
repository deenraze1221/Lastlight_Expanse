#include "Bunker/BunkerApp.h"

#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetRegistry.h"
#include "AssetManager/LiveScene.h"
#include "Assets/Primitives.h"
#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "SceneFile/SceneFile.h"
#include "World/World.h"
#include "WorldRender/SceneComponents.h"
#include "WorldRender/WorldRender.h"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace lastlight {
namespace {

using namespace zodar;
using namespace zodar::gfx;

constexpr const char* kLevel = "levels/bunker.scene.json";

class BunkerApp final : public Application {
public:
    BunkerApp(const ApplicationDesc& desc, std::filesystem::path assetDir)
        : Application(desc), m_assetDir(std::move(assetDir)) {}

protected:
    void onStart() override {
        AssetManagerDesc desc;
        desc.root = m_assetDir;
        desc.defaultSampler = SamplerDesc::pixelArt(); // GDD 3: nearest-neighbour pixel art
        desc.jobs = &jobs();
        desc.watchFiles = true; // hot reload of models, materials and textures (Debug builds only)
        m_assets = std::make_unique<AssetManager>(renderer().context(), upload(), scene(), desc);
        m_registry = AssetRegistry::load(m_assetDir / AssetRegistry::kFileName);
        {
            UploadBatch batch(upload());
            m_signMesh.emplace(batch, primitives::plane(0.8f, 0.8f)); // "sign" mesh of the level
            batch.submit();
        }
        // Start every model and material of the level decoding on the worker threads; the
        // LiveScene below then finds them in the AssetManager cache.
        const std::filesystem::path level = m_assets->resolve(kLevel);
        const SceneDocument doc = loadSceneFile(level);
        m_warmModels = m_assets->loadSceneModelsAsync(doc, level, &m_registry);
        m_warmMaterials = m_assets->createSceneMaterialsAsync(doc, level);
        if (Application::desc().deterministic) {
            m_assets->waitAll(); // reproducible frames: the level is complete in frame 0
        }
        m_view.camera.projection.nearPlane = 0.05f;
        tryLoadLevel();
    }

    void onEvent(const SDL_Event& e) override {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) m_looking = true;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) m_looking = false;
        if (e.type == SDL_EVENT_MOUSE_MOTION && m_looking) {
            m_yaw -= e.motion.xrel * 0.0035f;
            m_pitch = std::clamp(m_pitch - e.motion.yrel * 0.0035f, -1.55f, 1.55f);
        }
    }

    void onUpdate(const FrameTime& time) override {
        m_assets->update();
        tryLoadLevel();
        if (m_level && m_level->update()) {
            ZODAR_LOG_INFO("Level reloaded (generation {})", m_level->generation());
        }
        if (m_level && !m_level->error().empty() && m_level->error() != m_lastError) {
            ZODAR_LOG_WARN("Level reload failed, previous version kept: {}", m_level->error());
        }
        if (m_level) m_lastError = m_level->error();

        // Fly camera (until the player controller in D7). Speeds are bunker-scale.
        const float dt = static_cast<float>(time.dt);
        m_view.camera.setYawPitch(m_yaw, m_pitch);
        const bool* keys = SDL_GetKeyboardState(nullptr);
        glm::vec3 move(0.0f);
        if (keys[SDL_SCANCODE_W]) move += m_view.camera.forward();
        if (keys[SDL_SCANCODE_S]) move -= m_view.camera.forward();
        if (keys[SDL_SCANCODE_D]) move += m_view.camera.right();
        if (keys[SDL_SCANCODE_A]) move -= m_view.camera.right();
        if (keys[SDL_SCANCODE_SPACE]) move.y += 1.0f;
        if (keys[SDL_SCANCODE_LCTRL]) move.y -= 1.0f;
        if (glm::length(move) > 0.0f) {
            const float speed = keys[SDL_SCANCODE_LSHIFT] ? 8.0f : 3.0f;
            m_view.camera.position += glm::normalize(move) * speed * dt;
        }

        m_draws.clear();
        collectDraws(m_world, m_draws); // modelRenderer modules + meshRenderer signs (CPU path)
        collectLighting(m_world, m_view); // room lights, ambient, fog (re-read: hot reload)
        m_view.draws = m_draws;
        m_view.time = static_cast<float>(desc().deterministic ? time.time : time.realTime);
    }

    void onRender(RenderContext& rc) override {
        scene().addToGraph(rc.graph, rc.info, rc.extent, m_view, rc.output, rc.format, &rc.stats);
    }

    void onShutdown() override {
        renderer().waitIdle();
        if (m_level) {
            ZODAR_LOG_INFO("Bunker: {} entities, {} draws in the last frame", m_world.entityCount(),
                           lastRenderStats().drawCalls);
        }
        m_view.draws = {};
        m_draws.clear();
        m_level.reset();
        m_world.clear();
        m_warmModels = {};
        m_warmMaterials = {};
        renderer().context().collectGarbage();
        m_signMesh.reset();
        m_assets.reset();
    }

private:
    // Creates the LiveScene once the asynchronous loads finished (or failed).
    void tryLoadLevel() {
        if (m_level || m_warmModels.loading() || m_warmMaterials.loading()) {
            return;
        }
        if (!m_warmModels.errors().empty()) fail("bunker models failed: " + m_warmModels.errors().front());
        if (!m_warmMaterials.errors().empty()) fail("bunker materials failed: " + m_warmMaterials.errors().front());
        LiveSceneDesc live;
        live.registry = &m_registry;
        live.meshes["sign"] = &*m_signMesh;
        live.watch = true; // Debug builds: reload when the level or a prefab changes
        m_level = std::make_unique<LiveScene>(*m_assets, m_world, kLevel, live);
        if (!m_level->load()) {
            fail("cannot load " + std::string(kLevel) + ": " + m_level->error());
        }
        m_warmModels = {}; // the LiveScene holds its own handles now
        m_warmMaterials = {};

        // Start view: the level's camera (player start), or --camera.
        Camera start = m_view.camera;
        if (collectCamera(m_world, start)) {
            m_view.camera = start;
        }
        const glm::vec3 forward = m_view.camera.forward();
        m_yaw = std::atan2(-forward.x, -forward.z);
        m_pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
        if (desc().camera) {
            m_view.camera.position = desc().camera->position;
            m_yaw = glm::radians(desc().camera->yawDegrees);
            m_pitch = glm::radians(desc().camera->pitchDegrees);
        }
        ZODAR_LOG_INFO("Bunker loaded: {} entities ({} watched files{})", m_world.entityCount(),
                       m_level->files().size(), m_level->watching() ? ", hot reload on" : "");
    }

    std::filesystem::path m_assetDir;
    std::unique_ptr<AssetManager> m_assets; // declared first: destroyed after every handle
    AssetRegistry m_registry;
    std::optional<GpuMesh> m_signMesh;
    SceneModelAssets m_warmModels;
    SceneMaterialAssets m_warmMaterials;
    World m_world;
    std::unique_ptr<LiveScene> m_level; // destroyed first (destroys its entities)
    std::vector<MeshDraw> m_draws;
    SceneView m_view;
    std::string m_lastError;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_looking = false;
};

} // namespace

std::unique_ptr<Application> makeBunkerApp(const ApplicationDesc& desc, std::filesystem::path assetDir) {
    return std::make_unique<BunkerApp>(desc, std::move(assetDir));
}

} // namespace lastlight
