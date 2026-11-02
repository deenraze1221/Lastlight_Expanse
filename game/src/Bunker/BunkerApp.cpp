#include "Bunker/BunkerApp.h"

#include "Bunker/LevelColliders.h"
#include "Bunker/BunkerVoxels.h"
#include "AssetManager/BlockMaterials.h"
#include "Renderer/StreamedMeshSet.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Physics/CollisionWorld.h"
#include "Player/ExplorationWalker.h"

#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetRegistry.h"
#include "AssetManager/LiveScene.h"
#include "Assets/Primitives.h"
#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Platform/Window.h"
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
    BunkerApp(const ApplicationDesc& desc, std::filesystem::path assetDir, bool explore)
        : Application(desc), m_assetDir(std::move(assetDir)), m_explore(explore) {}

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
            m_signMesh.emplace(batch, primitives::plane(0.6f, 0.6f)); // "sign" mesh of the level
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
        if (m_explore) {
            m_colliderTable = loadColliderTable(m_assetDir / "colliders.json");
        }
        m_voxels = std::make_unique<BunkerVoxels>(m_assetDir, &jobs());
        m_blockMaterials = std::make_unique<BlockMaterials>(renderer().context(), upload(), scene(),
                                                          m_voxels->catalogue);
        m_voxelMeshes = std::make_unique<StreamedMeshSet>(renderer().context(), upload());
        for (const auto& e : m_voxels->world->takeMeshEvents()) {
            if (e.type != zodar::voxel::ChunkMeshEvent::Type::Upsert) continue;
            constexpr uint64_t mask = (1ull << 21) - 1;
            const auto& c = e.coord;
            const uint64_t key = (uint64_t(uint32_t(c.x)) & mask) |
                ((uint64_t(uint32_t(c.y)) & mask) << 21) | ((uint64_t(uint32_t(c.z)) & mask) << 42);
            m_voxelMeshes->upsert(key, e.mesh, glm::translate(glm::mat4(1), e.origin), m_blockMaterials->material());
        }
        m_voxelMeshes->update();
        upload().queue().waitIdle();
        m_voxelMeshes->update();
        tryLoadLevel();
    }

    void onFixedUpdate(double dt) override {
        if (!m_explore || !m_level) {
            return;
        }
        // Temporary keyboard polling (D3 replaces this with input actions).
        const bool* keys = SDL_GetKeyboardState(nullptr);
        WalkInput input;
        input.forward = float(keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) -
                        float(keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]);
        input.right = float(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) -
                      float(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
        input.sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        input.jump = keys[SDL_SCANCODE_SPACE];
        input.yaw = m_yaw;
        m_walker.step(m_collision, input, static_cast<float>(dt));
    }

    void onEvent(const SDL_Event& e) override {
        if (m_explore) {
            // Mouse look while captured; Tab releases/recaptures the mouse, a click recaptures.
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_TAB && !e.key.repeat) setMouseCaptured(!m_captured);
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured) setMouseCaptured(true);
            if (e.type == SDL_EVENT_MOUSE_MOTION && m_captured) {
                m_yaw -= e.motion.xrel * 0.0025f;
                m_pitch = std::clamp(m_pitch - e.motion.yrel * 0.0025f, -1.5f, 1.5f);
            }
            return;
        }
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
        if (m_explore && m_level && m_level->generation() != m_colliderGeneration) {
            rebuildColliders(); // the level was hot-reloaded: new geometry, same walker position
        }

        m_view.camera.setYawPitch(m_yaw, m_pitch);
        if (m_explore) {
            m_view.camera.position = m_walker.eye();
            finishFrame(time);
            return;
        }
        // Fly camera. Speeds are bunker-scale.
        const float dt = static_cast<float>(time.dt);
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
        finishFrame(time);
    }

    void finishFrame(const FrameTime& time) {
        m_draws.clear();
        m_voxelMeshes->appendDraws(m_draws);
        collectDraws(m_world, m_draws); // modelRenderer modules + meshRenderer signs (CPU path)
        collectLighting(m_world, m_view); // room lights, ambient, fog (re-read: hot reload)
        m_view.draws = m_draws;
        m_view.time = static_cast<float>(desc().deterministic ? time.time : time.realTime);
    }

    void onRender(RenderContext& rc) override {
        scene().addToGraph(rc.graph, rc.info, rc.extent, m_view, rc.output, rc.format, &rc.stats);
    }

    void onShutdown() override {
        setMouseCaptured(false);
        renderer().waitIdle();
        if (m_explore && m_level) {
            const glm::vec3 p = m_walker.feet();
            ZODAR_LOG_INFO("Explorer at ({:.2f}, {:.2f}, {:.2f}), {}", p.x, p.y, p.z,
                           m_walker.grounded() ? "on the ground" : "in the air");
            if (m_collision.overlaps(m_walker.box()) || p.y < -1.0f) {
                fail("exploration walker ended inside geometry or below the floor");
            }
        }
        if (m_level) {
            ZODAR_LOG_INFO("Bunker: {} entities, {} draws in the last frame", m_world.entityCount(),
                           lastRenderStats().drawCalls);
        }
        m_view.draws = {};
        m_draws.clear();
        m_voxelMeshes.reset();
        m_blockMaterials.reset();
        m_collision.setVoxelWorld(nullptr);
        m_voxels.reset();
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
        if (m_explore) {
            rebuildColliders();
            const WalkerSettings& w = m_walker.settings();
            m_walker.spawn(m_view.camera.position - glm::vec3(0.0f, w.eyeHeight, 0.0f));
            setMouseCaptured(true);
            ZODAR_LOG_INFO("Exploration mode: WASD move, mouse look, Shift sprint, Space jump, Tab frees the "
                           "mouse, Esc quits");
        }
    }

    void rebuildColliders() {
        m_collision.clear();
        m_collision.setVoxelWorld(m_voxels->world.get());
        const SceneBindings& bindings = m_level->bindings();
        const LevelColliderStats stats = buildLevelColliders(
            m_level->document(), m_colliderTable,
            [&](const std::string& model) -> std::optional<Aabb> {
                const auto it = bindings.models.find(model);
                if (it == bindings.models.end() || !it->second || it->second->bounds().isEmpty()) return std::nullopt;
                return Aabb{it->second->bounds().min, it->second->bounds().max};
            },
            m_collision);
        m_colliderGeneration = m_level->generation();
        ZODAR_LOG_INFO("Colliders: {} boxes ({} entities from colliders.json, {} from model bounds, {} without)",
                       stats.boxes, stats.fromTable, stats.fromBounds, stats.skipped);
    }

    void setMouseCaptured(bool captured) {
        if (desc().headless || captured == m_captured) {
            return;
        }
        SDL_SetWindowRelativeMouseMode(window().native(), captured);
        m_captured = captured;
    }

    std::filesystem::path m_assetDir;
    std::unique_ptr<AssetManager> m_assets; // declared first: destroyed after every handle
    AssetRegistry m_registry;
    std::optional<GpuMesh> m_signMesh;
    SceneModelAssets m_warmModels;
    SceneMaterialAssets m_warmMaterials;
    std::unique_ptr<BunkerVoxels> m_voxels;
    std::unique_ptr<BlockMaterials> m_blockMaterials;
    std::unique_ptr<StreamedMeshSet> m_voxelMeshes;
    World m_world;
    std::unique_ptr<LiveScene> m_level; // destroyed first (destroys its entities)
    std::vector<MeshDraw> m_draws;
    SceneView m_view;
    std::string m_lastError;
    bool m_explore = false;
    ColliderTable m_colliderTable;
    CollisionWorld m_collision;
    ExplorationWalker m_walker;
    uint64_t m_colliderGeneration = 0;
    bool m_captured = false;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_looking = false;
};

} // namespace

std::unique_ptr<Application> makeBunkerApp(const ApplicationDesc& desc, std::filesystem::path assetDir, bool explore) {
    return std::make_unique<BunkerApp>(desc, std::move(assetDir), explore);
}

} // namespace lastlight
