#include <nlohmann/json.hpp>
#include "Bunker/BunkerApp.h"
#include "GameInput.h"

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


#include <glm/glm.hpp>

#include <array>
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
    BunkerApp(const ApplicationDesc& desc, std::filesystem::path assetDir, bool explore, bool walkthrough)
        : Application(desc), m_assetDir(std::move(assetDir)), m_explore(explore), m_walkthrough(walkthrough) {}

protected:
    std::string inputLevelHash() const override {
        return inputHash(bunkerContentHash(m_assetDir)+(m_explore?"explore":"fly")+(m_walkthrough?"walkthrough":"manual"));
    }
    std::string inputState() const override {
        return playerFingerprint(m_explore?m_walker.feet():m_view.camera.position,m_yaw,m_pitch,
                                 m_explore?m_walker.velocity():glm::vec3(0),m_explore&&m_walker.grounded())+
               ":"+std::to_string(m_waypoint)+":"+std::to_string(m_captured);
    }
    SaveGameDesc saveGameDesc() const override { auto settings=Application::saveGameDesc(); settings.scenePath=kLevel; settings.sceneDirectory=std::filesystem::absolute(m_assetDir/std::filesystem::path(kLevel).parent_path()); return settings; }
    SaveGameData captureGameSave() const override {
        if (!m_level) fail("cannot save before bunker is ready");
        SaveGameData data;
        data.world=captureScene(m_world,invertBindings(m_level->document(),m_level->bindings()));
        data.game["bunker"]=nlohmann::json{{"position",{m_view.camera.position.x,m_view.camera.position.y,m_view.camera.position.z}},
            {"yaw",m_yaw},{"pitch",m_pitch},{"walker",nlohmann::json::parse(m_walker.capture())},
            {"waypoint",m_waypoint},{"captured",m_captured}}.dump();
        return data;
    }
    std::unique_ptr<SaveCommit> prepareGameSave(const SaveGameData& data) override {
        if (!m_level || data.game.size()!=1) fail("invalid bunker save handlers");
        const auto j=nlohmann::json::parse(data.game.at("bunker"));
        if (!j.is_object() || j.size()!=6 || !j.at("position").is_array() || j.at("position").size()!=3 ||
            !j.at("captured").is_boolean() || !j.at("waypoint").is_number_unsigned()) fail("invalid bunker save");
        glm::vec3 position;
        for (int i=0;i<3;++i) {
            const auto& v=j.at("position")[static_cast<size_t>(i)];
            if (!v.is_number() || (!std::isfinite(v.get<float>()) || std::abs(v.get<float>())>1000000.0f)) fail("invalid saved camera");
            position[i]=v.get<float>();
        }
        const float yaw=j.at("yaw").get<float>(), pitch=j.at("pitch").get<float>();
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) fail("invalid saved orientation");
        auto walker=m_walker.stage(j.at("walker").dump());
        const auto waypoint=j.at("waypoint").get<size_t>();
        if (waypoint>m_route.size()) fail("invalid saved route cursor");
        const bool captured=j.at("captured").get<bool>();
        const auto state=playerFingerprint(m_explore?walker.feet():position,yaw,pitch,
            m_explore?walker.velocity():glm::vec3(0),m_explore&&walker.grounded())+
            ":"+std::to_string(waypoint)+":"+std::to_string(captured);
        struct Stage { World world; SceneDocument document; SceneInstance instance; CollisionWorld collision; };
        auto staged=std::make_shared<Stage>(); staged->document=data.world;
        staged->instance=instantiateScene(data.world,staged->world,m_level->bindings());
        if (m_explore) {
            staged->collision.setVoxelWorld(m_voxels->world.get());
            (void)buildLevelColliders(data.world,m_colliderTable,[&](const std::string& model)->std::optional<Aabb> {
                const auto it=m_level->bindings().models.find(model);
                if (it==m_level->bindings().models.end() || !it->second || it->second->bounds().isEmpty()) return std::nullopt;
                return Aabb{it->second->bounds().min,it->second->bounds().max};
            },staged->collision);
        }
        return makeSaveCommit([this,staged,position,yaw,pitch,walker,waypoint,captured]() noexcept {
            m_level->commitSave(std::move(staged->world),std::move(staged->document),std::move(staged->instance));
            m_view.camera.position=position; m_yaw=yaw; m_pitch=pitch; m_walker=walker;
            m_waypoint=waypoint; m_captured=captured;
            if (m_explore) m_collision=std::move(staged->collision);
            m_colliderGeneration=m_level->generation();
        },state);
    }
    void onStart() override {
        input().setContext("explore", m_explore);
        gameplay().systems().add({"Bunker.Controls", GameplayPhase::FixedSimulation,
            [this](const SystemContext& c) { updateControls(c.dt); }});
        gameplay().systems().add({"Bunker.CameraSync", GameplayPhase::RenderSync,
            [this](const SystemContext&) {
                m_view.camera.setYawPitch(m_yaw, m_pitch);
                if (m_explore) m_view.camera.position = m_walker.eye();
            }});
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
        if (Application::desc().deterministic || Application::desc().recordInput || Application::desc().loadSave || Application::desc().saveAtExit) {
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
        auto meshEvents = m_voxels->world->takeMeshEvents();
        std::stable_sort(meshEvents.begin(), meshEvents.end(), [](const auto& a, const auto& b) {
            return a.coord < b.coord;
        });
        for (const auto& e : meshEvents) {
            if (e.type != zodar::voxel::ChunkMeshEvent::Type::Upsert) continue;
            constexpr uint64_t mask = (1ull << 21) - 1;
            const auto& c = e.coord;
            const uint64_t key = (uint64_t(uint32_t(c.x)) & mask) |
                ((uint64_t(uint32_t(c.y)) & mask) << 21) | ((uint64_t(uint32_t(c.z)) & mask) << 42);
            m_voxelMeshes->upsert(key, e.mesh, glm::translate(glm::mat4(1), e.origin), m_blockMaterials->material());
        }
        m_voxelMeshes->update();
        upload().queue().wait({upload().queue().stats().submitted});
        m_voxelMeshes->update();
        const auto voxelStats = m_voxelMeshes->stats();
        ZODAR_ENSURE(voxelStats.meshes > 0 && voxelStats.pendingUploads == 0,
                     "bunker voxel meshes not ready");
        ZODAR_LOG_INFO("Bunker voxels: {} meshes, {} triangles", voxelStats.meshes, voxelStats.triangles);
        tryLoadLevel();
    }

    void updateControls(double dt) {
        if (!m_level) return;
        if (m_explore && input().action("capture_toggle").pressed) setMouseCaptured(!m_captured);
        if (input().action("dig").pressed && m_explore && !m_captured) setMouseCaptured(true);
        if ((m_explore && m_captured) || (!m_explore && (input().action("look_hold").held || input().action("look_hold").pressed))) {
            const float sensitivity = m_explore ? 0.0025f : 0.0035f;
            m_yaw -= input().action("look").x * sensitivity;
            m_pitch = std::clamp(m_pitch - input().action("look").y * sensitivity, -1.5f, 1.5f);
        }
        if (!m_explore) {
            m_view.camera.setYawPitch(m_yaw, m_pitch);
            glm::vec3 move = m_view.camera.forward() * input().action("move").y +
                             m_view.camera.right() * input().action("move").x;
            move.y += input().action("vertical").x;
            if (glm::length(move) > 0) m_view.camera.position += glm::normalize(move) *
                (input().action("sprint").held ? 8.0f : 3.0f) * static_cast<float>(dt);
            return;
        }
        WalkInput input;
        input.forward = this->input().action("move").y;
        input.right = this->input().action("move").x;
        input.sprint = this->input().action("sprint").held;
        input.jump = this->input().action("jump").held;
        if (m_walkthrough) {
            if (m_waypoint < m_route.size()) {
                const glm::vec2 target = m_route[m_waypoint];
                const glm::vec2 delta(target.x - m_walker.feet().x, target.y - m_walker.feet().z);
                if (glm::length(delta) < 0.25f) ++m_waypoint;
                else {
                    m_yaw = std::atan2(-delta.x, -delta.y);
                    input.forward = 1;
                }
            }
        }
        input.yaw = m_yaw;
        m_walker.step(m_collision, input, static_cast<float>(dt));
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
        if (m_walkthrough) {
            if (m_waypoint != m_route.size()) fail("bunker walkthrough did not finish");
            ZODAR_LOG_INFO("Bunker walkthrough complete: {} waypoints", m_waypoint);
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
        m_captured = captured;
        input().setRelativeMouse(captured);
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
    bool m_walkthrough = false;
    size_t m_waypoint = 0;
    // Real fixed-step movement, including stairs, airlock, a room and full corridor.
    const std::array<glm::vec2, 8> m_route{{{-15, 0}, {-11, 0}, {-4, 0}, {6, 0},
                                         {6, 3.5f}, {6, 0}, {38, 0}, {58, 0}}};
    ColliderTable m_colliderTable;
    CollisionWorld m_collision;
    ExplorationWalker m_walker;
    uint64_t m_colliderGeneration = 0;
    bool m_captured = false;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
};

} // namespace

std::unique_ptr<Application> makeBunkerApp(const ApplicationDesc& desc, std::filesystem::path assetDir, bool explore, bool walkthrough) {
    return std::make_unique<BunkerApp>(desc, std::move(assetDir), explore, walkthrough);
}

} // namespace lastlight
