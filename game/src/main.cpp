// Lastlight Expanse - streamed voxel highlands at dusk, built on the ZODAR engine.
//
//   LastlightExpanse [--seed N] [--view-radius CHUNKS] [--threads N] [--frames N] [--screenshot out.ppm]
//                    [--width W] [--height H] [--no-validation] [--no-overlay]
//                    [--lighting-quality low|medium|high|ultra] [--untextured] [--no-fog]
//                    [--spawn-height H] [--pitch DEG] [--headless] [--deterministic]
//
// Controls: hold right mouse to look, WASD move, Space/Ctrl up/down, Shift fast,
// left click digs the targeted voxel, F places stone on it, F1 profiler, Esc quits.
// --headless: no visible window (needs --frames). --deterministic: fixed 1/60 s per frame.
// Exit codes: 0 ok, 1 fatal error, 2 validation errors, 3 screenshot failed.
// Runs on zodar::Application (engine/runtime): it owns the window, renderer and frame loop.

#include "Rendering/BlockTextures.h"
#include "World/LastlightTerrain.h"

#include "Assets/EnvironmentMaps.h"
#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/StreamedMeshSet.h"
#include "Renderer/TextureArray.h"
#include "Runtime/Application.h"
#include "Scene/Voxel/VoxelWorld.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace zodar;
using namespace zodar::gfx;
using namespace zodar::voxel;

struct Options {
    uint64_t seed = 1337;
    int viewRadius = 10;
    uint32_t threads = JobSystem::kAutomatic;
    uint64_t maxFrames = 0;
    std::optional<std::filesystem::path> screenshot;
    uint32_t width = 1600;
    uint32_t height = 900;
    bool validation = true;
    bool overlay = false;
    LightingQuality quality = LightingQuality::High;
    bool textured = true; // pixel-art texture arrays (false: flat palette colours, A/B tests)
    bool fog = true;
    float spawnHeight = 14.0f; // above the terrain surface
    float pitch = -12.0f;      // degrees
    bool headless = false;
    bool deterministic = false;
};

Options parseOptions(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) fail("Missing value for " + std::string(arg));
            return argv[++i];
        };
        if (arg == "--seed") o.seed = std::stoull(next());
        else if (arg == "--view-radius") o.viewRadius = std::stoi(next());
        else if (arg == "--threads") o.threads = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "--frames") o.maxFrames = std::stoull(next());
        else if (arg == "--screenshot") o.screenshot = next();
        else if (arg == "--width") o.width = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "--height") o.height = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "--no-validation") o.validation = false;
        else if (arg == "--no-overlay") o.overlay = false;
        else if (arg == "--overlay") o.overlay = true;
        else if (arg == "--lighting-quality") o.quality = parseLightingQuality(next());
        else if (arg == "--untextured") o.textured = false;
        else if (arg == "--no-fog") o.fog = false;
        else if (arg == "--spawn-height") o.spawnHeight = std::stof(next());
        else if (arg == "--pitch") o.pitch = std::stof(next());
        else if (arg == "--headless") o.headless = true;
        else if (arg == "--deterministic") o.deterministic = true;
        else fail("Unknown argument '" + std::string(arg) + "'");
    }
    return o;
}

uint64_t chunkKey(const ChunkCoord& c) {
    constexpr uint64_t m = (1ull << 21) - 1;
    return (uint64_t(uint32_t(c.x)) & m) | ((uint64_t(uint32_t(c.y)) & m) << 21) | ((uint64_t(uint32_t(c.z)) & m) << 42);
}

ApplicationDesc makeDesc(const Options& options) {
    ApplicationDesc desc;
    desc.name = "Lastlight Expanse";
    desc.organization = "Lastlight";
    desc.identifier = "LastlightExpanse";
    desc.width = options.width;
    desc.height = options.height;
    desc.headless = options.headless;
    desc.validation = options.validation;
    desc.overlay = options.overlay;
    desc.maxFrames = options.maxFrames;
    desc.screenshot = options.screenshot;
    desc.deterministic = options.deterministic;
    desc.threads = options.threads;
    return desc;
}

class LastlightApp final : public Application {
public:
    explicit LastlightApp(const Options& options) : Application(makeDesc(options)), m_options(options) {}

protected:
    void onStart() override;
    void onEvent(const SDL_Event& e) override;
    void onUpdate(const FrameTime& time) override;
    void onRender(RenderContext& rc) override;
    void onShutdown() override;

private:
    void logStats(double fps);

    Options m_options;
    std::shared_ptr<lastlight::LastlightTerrain> m_terrain;
    StreamingSettings m_streaming;
    std::unique_ptr<VoxelWorld> m_world;
    std::unique_ptr<StreamedMeshSet> m_chunkMeshes;
    TextureArray m_standardTextures;
    TextureArray m_heroTextures;
    MaterialHandle m_terrainMaterial{};
    std::unique_ptr<Environment> m_environment;
    SceneView m_view;
    std::vector<MeshDraw> m_draws;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_looking = false;
    bool m_digRequested = false;
    bool m_placeRequested = false;
    double m_statsElapsed = 0.0;
    uint64_t m_statsFrames = 0;
};

void LastlightApp::onStart() {
    // --- World ------------------------------------------------------------------------
    m_terrain = std::make_shared<lastlight::LastlightTerrain>(m_options.seed);
    m_streaming.viewRadius = m_options.viewRadius;
    m_streaming.minChunkY = -2;
    m_streaming.maxChunkY = 3;
    m_streaming.lodDistances = {3.0f, 6.0f, 11.0f};
    VoxelPalette palette = lastlight::makePalette();
    if (m_options.textured) {
        lastlight::assignFaceLayers(palette);
    }
    m_world = std::make_unique<VoxelWorld>(m_terrain, palette, m_streaming, &jobs());
    m_chunkMeshes = std::make_unique<StreamedMeshSet>(renderer().context(), upload());
    ZODAR_LOG_INFO("World seed {}, view radius {} chunks, {} worker threads", m_options.seed, m_options.viewRadius,
                   jobs().workerCount());

    MaterialDesc ground;
    ground.name = "Terrain";
    ground.baseColorFactor = glm::vec4(1.0f); // voxel colours come from vertex colours
    ground.roughness = 0.92f;
    // Pixel-art look: block textures in two arrays (256^2 standard, 512^2 hero) sampled
    // point-filtered by the game's voxel shader; one material and one descriptor for all blocks.
    if (m_options.textured) {
        const auto start = std::chrono::steady_clock::now();
        const lastlight::BlockTextureSet textures = lastlight::generateBlockTextures(m_options.seed);
        UploadBatch batch(upload());
        m_standardTextures = TextureArray(batch, scene().bindless(), textures.standard, "BlockTextures256");
        m_heroTextures = TextureArray(batch, scene().bindless(), textures.hero, "BlockTextures512");
        batch.submit();
        SamplerDesc crisp;
        crisp.magFilter = Filter::Nearest; // hard texel edges up close
        crisp.minFilter = Filter::Nearest;
        crisp.mipFilter = Filter::Linear;  // mips stop distant shimmer
        crisp.maxAnisotropy = 1.0f;
        ground.shader = scene().materials().registerShader("lastlight/voxel", "mesh.vert.spv", "voxel.frag.spv");
        ground.custom0 = {std::bit_cast<float>(m_standardTextures.bindlessIndex()),
                          std::bit_cast<float>(scene().bindless().sampler(crisp)),
                          std::bit_cast<float>(m_heroTextures.bindlessIndex()), 0.0f};
        ground.custom1 = {4.0f, 1.0f / m_streaming.voxelSize, 0.0f, 0.0f}; // emissive strength, voxels per unit
        ZODAR_LOG_INFO("Block textures: {} x {}^2 + {} x {}^2 in {:.0f} ms", textures.standard.size(),
                       lastlight::kStandardTextureSize, textures.hero.size(), lastlight::kHeroTextureSize,
                       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    m_terrainMaterial = scene().materials().createMaterial(ground);

    LightingSettings lightingSettings;
    lightingSettings.quality = m_options.quality;
    scene().setLightingSettings(lightingSettings);

    // Dusk: a low, warm sun - the "last light" over the expanse.
    const glm::vec3 sunDirection = glm::normalize(glm::vec3(-0.35f, -0.22f, -0.9f));
    ProceduralSkyDesc sky;
    sky.zenithColor = {0.12f, 0.16f, 0.35f};
    sky.horizonColor = {0.95f, 0.5f, 0.28f};
    sky.groundColor = {0.08f, 0.06f, 0.05f};
    sky.sunDirection = sunDirection;
    sky.sunColor = {1.0f, 0.55f, 0.3f};
    m_environment = scene().ibl().bake(proceduralSky(1024, sky), iblSettings(m_options.quality));

    m_view.clearColor = {0.05f, 0.04f, 0.06f, 1.0f};
    m_view.lighting.environment = m_environment.get();
    m_view.lighting.environmentIntensity = 0.6f;
    m_view.lighting.sun = {sunDirection, {1.0f, 0.62f, 0.38f}, 3.5f, true};
    m_view.camera.projection.nearPlane = 0.1f;
    // Dusk haze: dense in the valleys, thinning with altitude; never fully hides the far ridges.
    m_view.lighting.fog.enabled = m_options.fog;
    m_view.lighting.fog.color = {0.55f, 0.36f, 0.3f};
    m_view.lighting.fog.density = 0.006f;
    m_view.lighting.fog.heightFalloff = 0.025f;
    m_view.lighting.fog.baseHeight = 10.0f;
    m_view.lighting.fog.startDistance = 24.0f;
    m_view.lighting.fog.maxOpacity = 0.85f;

    // Spawn above the terrain at the origin.
    const int spawnHeight = m_terrain->surfaceHeight(0, 0);
    m_view.camera.position = {0.5f, static_cast<float>(spawnHeight) + m_options.spawnHeight, 0.5f};
    m_yaw = glm::radians(200.0f);
    m_pitch = glm::radians(m_options.pitch);

    // Loading: stream the area around the spawn point before the first frame.
    const auto start = std::chrono::steady_clock::now();
    while (!m_world->isSettled()) {
        m_world->update(m_view.camera.position);
        m_world->finishPendingWork();
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(60)) {
            ZODAR_LOG_WARN("World not settled after 60 s; continuing while streaming");
            break;
        }
    }
    const VoxelWorldStats s = m_world->stats();
    ZODAR_LOG_INFO("World ready in {:.0f} ms: {} chunks with data, {} generated, {} meshed",
                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
                   s.chunksWithData, s.generated, s.meshed);
}

void LastlightApp::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F && !e.key.repeat) m_placeRequested = true;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) m_looking = true;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) m_looking = false;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) m_digRequested = true;
    if (e.type == SDL_EVENT_MOUSE_MOTION && m_looking) {
        m_yaw -= e.motion.xrel * 0.0035f;
        m_pitch = std::clamp(m_pitch - e.motion.yrel * 0.0035f, -1.55f, 1.55f);
    }
}

void LastlightApp::onUpdate(const FrameTime& time) {
    const float dt = static_cast<float>(time.dt);

    // Fly camera.
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
        const float speed = keys[SDL_SCANCODE_LSHIFT] ? 60.0f : 15.0f;
        m_view.camera.position += glm::normalize(move) * speed * dt;
    }

    // Interaction: dig / place at the targeted voxel.
    if (m_digRequested || m_placeRequested) {
        if (auto hit = m_world->raycast(m_view.camera.position, m_view.camera.forward(), 64.0f)) {
            if (m_digRequested) m_world->setVoxel(hit->voxel, lastlight::Air);
            else m_world->setVoxel(hit->voxel + hit->normal, lastlight::Stone);
        }
        m_digRequested = m_placeRequested = false;
    }

    // Streaming: integrate finished chunks, schedule new ones, apply mesh changes.
    {
        ZODAR_CPU_SCOPE(cpuProfiler(), "World streaming");
        m_world->update(m_view.camera.position);
        for (ChunkMeshEvent& e : m_world->takeMeshEvents()) {
            if (e.type == ChunkMeshEvent::Type::Upsert) {
                m_chunkMeshes->upsert(chunkKey(e.coord), e.mesh, glm::translate(glm::mat4(1.0f), e.origin),
                                      m_terrainMaterial);
            } else {
                m_chunkMeshes->remove(chunkKey(e.coord));
            }
        }
        m_chunkMeshes->update();
        m_draws.clear();
        m_chunkMeshes->appendDraws(m_draws);
    }
    m_view.draws = m_draws;
    // Wall-clock time for visuals (as before the runtime migration); fixed clock when deterministic.
    m_view.time = static_cast<float>(m_options.deterministic ? time.time : time.realTime);

    ++m_statsFrames;
    m_statsElapsed += time.realDt;
    if (m_statsElapsed >= 2.0) {
        logStats(m_statsFrames / m_statsElapsed);
        m_statsElapsed = 0.0;
        m_statsFrames = 0;
    }
}

void LastlightApp::onRender(RenderContext& rc) {
    scene().addToGraph(rc.graph, rc.info, rc.extent, m_view, rc.output, rc.format, &rc.stats);
}

void LastlightApp::logStats(double fps) {
    const VoxelWorldStats ws = m_world->stats();
    const StreamedMeshSet::Stats ms = m_chunkMeshes->stats();
    const RenderStats& rs = lastRenderStats();
    ZODAR_LOG_INFO("{:.1f} FPS | chunks {} (LOD0-3: {}/{}/{}/{}), {} pending, {:.1f} M tris, {:.0f} MB voxels, "
                   "{:.0f} MB meshes | draws {} culled {}",
                   fps, ms.meshes, ws.lodHistogram[0], ws.lodHistogram[1], ws.lodHistogram[2], ws.lodHistogram[3],
                   ws.generateInFlight + ws.meshInFlight, double(ms.triangles) / 1e6,
                   double(ws.voxelBytes) / 1048576.0, double(ms.gpuBytes) / 1048576.0, rs.drawCalls, rs.culled);
}

void LastlightApp::onShutdown() {
    if (!m_chunkMeshes) return;
    const StreamedMeshSet::Stats ms = m_chunkMeshes->stats();
    ZODAR_LOG_INFO("Rendered {} frames; {} chunk meshes, {} triangles, last frame {} draw calls ({} culled)",
                   renderer().frameNumber(), ms.meshes, ms.triangles, lastRenderStats().drawCalls,
                   lastRenderStats().culled);
}

} // namespace

int main(int argc, char** argv) {
    zodar::log::init({spdlog::level::info, ""});
    int exitCode = EXIT_FAILURE;
    try {
        LastlightApp app(parseOptions(argc, argv));
        exitCode = app.run();
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
    }
    ZODAR_LOG_INFO("Shutdown complete (exit code {})", exitCode);
    zodar::log::shutdown();
    return exitCode;
}
