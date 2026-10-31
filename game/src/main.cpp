// Lastlight Expanse - streamed voxel highlands at dusk, built on the ZODAR engine.
//
//   LastlightExpanse [--seed N] [--view-radius CHUNKS] [--threads N] [--frames N] [--screenshot out.ppm]
//                    [--width W] [--height H] [--no-validation] [--no-overlay]
//                    [--lighting-quality low|medium|high|ultra] [--untextured] [--no-fog]
//                    [--spawn-height H] [--pitch DEG]
//
// Controls: hold right mouse to look, WASD move, Space/Ctrl up/down, Shift fast,
// left click digs the targeted voxel, F places stone on it, F1 profiler, Esc quits.
// Exit codes: 0 ok, 1 fatal error, 2 validation errors, 3 screenshot failed.

#include "Rendering/BlockTextures.h"
#include "World/LastlightTerrain.h"

#include "Assets/EnvironmentMaps.h"
#include "Core/Error.h"
#include "Core/JobSystem.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Debug/DebugOverlay.h"
#include "Platform/Platform.h"
#include "Platform/Window.h"
#include "Renderer/GpuProfiler.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/StreamedMeshSet.h"
#include "Renderer/TextureArray.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
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
        else fail("Unknown argument '" + std::string(arg) + "'");
    }
    return o;
}

uint64_t chunkKey(const ChunkCoord& c) {
    constexpr uint64_t m = (1ull << 21) - 1;
    return (uint64_t(uint32_t(c.x)) & m) | ((uint64_t(uint32_t(c.y)) & m) << 21) | ((uint64_t(uint32_t(c.z)) & m) << 42);
}

int run(const Options& options) {
    Platform platform;
    Window window({"Lastlight Expanse", options.width, options.height, true});
    Renderer renderer(window, {"Lastlight Expanse", options.validation, PresentMode::VSync,
                               Platform::userDataPath("Lastlight", "LastlightExpanse") / "pipeline_cache.bin"});
    UploadContext upload(renderer.context());
    renderer.attachUploads(&upload.queue());

    const char* base = SDL_GetBasePath();
    SceneRenderer scene(renderer.context(), upload,
                        {std::filesystem::path(base ? base : "") / "shaders", VK_FORMAT_R16G16B16A16_SFLOAT,
                         VK_FORMAT_D32_SFLOAT, Renderer::kFramesInFlight});
    RenderGraphResources graphResources(renderer.context(), scene.bindless());
    debug::DebugOverlay overlay(renderer.context(), &window, renderer.swapchain().format(),
                                renderer.swapchain().imageCount());
    overlay.visible = options.overlay;
    CpuProfiler cpuProfiler;
    History cpuFrameMs;
    History gpuFrameMs;

    // --- World ------------------------------------------------------------------------
    JobSystem jobs(options.threads);
    auto terrain = std::make_shared<lastlight::LastlightTerrain>(options.seed);
    StreamingSettings streaming;
    streaming.viewRadius = options.viewRadius;
    streaming.minChunkY = -2;
    streaming.maxChunkY = 3;
    streaming.lodDistances = {3.0f, 6.0f, 11.0f};
    VoxelPalette palette = lastlight::makePalette();
    if (options.textured) {
        lastlight::assignFaceLayers(palette);
    }
    VoxelWorld world(terrain, palette, streaming, &jobs);
    StreamedMeshSet chunkMeshes(renderer.context(), upload);
    ZODAR_LOG_INFO("World seed {}, view radius {} chunks, {} worker threads", options.seed, options.viewRadius,
                   jobs.workerCount());

    MaterialDesc ground;
    ground.name = "Terrain";
    ground.baseColorFactor = glm::vec4(1.0f); // voxel colours come from vertex colours
    ground.roughness = 0.92f;
    // Pixel-art look: block textures in two arrays (256^2 standard, 512^2 hero) sampled
    // point-filtered by the game's voxel shader; one material and one descriptor for all blocks.
    TextureArray standardTextures;
    TextureArray heroTextures;
    if (options.textured) {
        const auto start = std::chrono::steady_clock::now();
        const lastlight::BlockTextureSet textures = lastlight::generateBlockTextures(options.seed);
        UploadBatch batch(upload);
        standardTextures = TextureArray(batch, scene.bindless(), textures.standard, "BlockTextures256");
        heroTextures = TextureArray(batch, scene.bindless(), textures.hero, "BlockTextures512");
        batch.submit();
        SamplerDesc crisp;
        crisp.magFilter = Filter::Nearest; // hard texel edges up close
        crisp.minFilter = Filter::Nearest;
        crisp.mipFilter = Filter::Linear;  // mips stop distant shimmer
        crisp.maxAnisotropy = 1.0f;
        ground.shader = scene.materials().registerShader("lastlight/voxel", "mesh.vert.spv", "voxel.frag.spv");
        ground.custom0 = {std::bit_cast<float>(standardTextures.bindlessIndex()),
                          std::bit_cast<float>(scene.bindless().sampler(crisp)),
                          std::bit_cast<float>(heroTextures.bindlessIndex()), 0.0f};
        ground.custom1 = {4.0f, 1.0f / streaming.voxelSize, 0.0f, 0.0f}; // emissive strength, voxels per unit
        ZODAR_LOG_INFO("Block textures: {} x {}^2 + {} x {}^2 in {:.0f} ms", textures.standard.size(),
                       lastlight::kStandardTextureSize, textures.hero.size(), lastlight::kHeroTextureSize,
                       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    const MaterialHandle terrainMaterial = scene.materials().createMaterial(ground);

    LightingSettings lightingSettings;
    lightingSettings.quality = options.quality;
    scene.setLightingSettings(lightingSettings);

    // Dusk: a low, warm sun - the "last light" over the expanse.
    const glm::vec3 sunDirection = glm::normalize(glm::vec3(-0.35f, -0.22f, -0.9f));
    ProceduralSkyDesc sky;
    sky.zenithColor = {0.12f, 0.16f, 0.35f};
    sky.horizonColor = {0.95f, 0.5f, 0.28f};
    sky.groundColor = {0.08f, 0.06f, 0.05f};
    sky.sunDirection = sunDirection;
    sky.sunColor = {1.0f, 0.55f, 0.3f};
    const auto environment = scene.ibl().bake(proceduralSky(1024, sky), iblSettings(options.quality));

    SceneView view;
    view.clearColor = {0.05f, 0.04f, 0.06f, 1.0f};
    view.lighting.environment = environment.get();
    view.lighting.environmentIntensity = 0.6f;
    view.lighting.sun = {sunDirection, {1.0f, 0.62f, 0.38f}, 3.5f, true};
    view.camera.projection.nearPlane = 0.1f;
    // Dusk haze: dense in the valleys, thinning with altitude; never fully hides the far ridges.
    view.lighting.fog.enabled = options.fog;
    view.lighting.fog.color = {0.55f, 0.36f, 0.3f};
    view.lighting.fog.density = 0.006f;
    view.lighting.fog.heightFalloff = 0.025f;
    view.lighting.fog.baseHeight = 10.0f;
    view.lighting.fog.startDistance = 24.0f;
    view.lighting.fog.maxOpacity = 0.85f;

    // Spawn above the terrain at the origin.
    const int spawnHeight = terrain->surfaceHeight(0, 0);
    view.camera.position = {0.5f, static_cast<float>(spawnHeight) + options.spawnHeight, 0.5f};
    float yaw = glm::radians(200.0f);
    float pitch = glm::radians(options.pitch);

    // Loading: stream the area around the spawn point before the first frame.
    {
        const auto start = std::chrono::steady_clock::now();
        while (!world.isSettled()) {
            world.update(view.camera.position);
            world.finishPendingWork();
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(60)) {
                ZODAR_LOG_WARN("World not settled after 60 s; continuing while streaming");
                break;
            }
        }
        const VoxelWorldStats s = world.stats();
        ZODAR_LOG_INFO("World ready in {:.0f} ms: {} chunks with data, {} generated, {} meshed",
                       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
                       s.chunksWithData, s.generated, s.meshed);
    }

    bool looking = false;
    bool digRequested = false;
    bool placeRequested = false;
    std::vector<MeshDraw> draws;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start;
    auto statsStart = start;
    uint64_t statsFrames = 0;
    RenderStats lastStats;

    while (!window.shouldClose()) {
        cpuProfiler.beginFrame();
        window.pollEvents([&](const SDL_Event& e) {
            if (overlay.processEvent(e)) return;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) window.requestClose();
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F && !e.key.repeat) placeRequested = true;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) looking = true;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) looking = false;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) digRequested = true;
            if (e.type == SDL_EVENT_MOUSE_MOTION && looking) {
                yaw -= e.motion.xrel * 0.0035f;
                pitch = std::clamp(pitch - e.motion.yrel * 0.0035f, -1.55f, 1.55f);
            }
        });

        const auto now = Clock::now();
        const float dt = std::min(std::chrono::duration<float>(now - last).count(), 0.1f);
        last = now;

        // Fly camera.
        view.camera.setYawPitch(yaw, pitch);
        const bool* keys = SDL_GetKeyboardState(nullptr);
        glm::vec3 move(0.0f);
        if (keys[SDL_SCANCODE_W]) move += view.camera.forward();
        if (keys[SDL_SCANCODE_S]) move -= view.camera.forward();
        if (keys[SDL_SCANCODE_D]) move += view.camera.right();
        if (keys[SDL_SCANCODE_A]) move -= view.camera.right();
        if (keys[SDL_SCANCODE_SPACE]) move.y += 1.0f;
        if (keys[SDL_SCANCODE_LCTRL]) move.y -= 1.0f;
        if (glm::length(move) > 0.0f) {
            const float speed = keys[SDL_SCANCODE_LSHIFT] ? 60.0f : 15.0f;
            view.camera.position += glm::normalize(move) * speed * dt;
        }

        // Interaction: dig / place at the targeted voxel.
        if (digRequested || placeRequested) {
            if (auto hit = world.raycast(view.camera.position, view.camera.forward(), 64.0f)) {
                if (digRequested) world.setVoxel(hit->voxel, lastlight::Air);
                else world.setVoxel(hit->voxel + hit->normal, lastlight::Stone);
            }
            digRequested = placeRequested = false;
        }

        // Streaming: integrate finished chunks, schedule new ones, apply mesh changes.
        {
            ZODAR_CPU_SCOPE(cpuProfiler, "World streaming");
            world.update(view.camera.position);
            for (ChunkMeshEvent& e : world.takeMeshEvents()) {
                if (e.type == ChunkMeshEvent::Type::Upsert) {
                    chunkMeshes.upsert(chunkKey(e.coord), e.mesh, glm::translate(glm::mat4(1.0f), e.origin),
                                       terrainMaterial);
                } else {
                    chunkMeshes.remove(chunkKey(e.coord));
                }
            }
            chunkMeshes.update();
            draws.clear();
            chunkMeshes.appendDraws(draws);
        }
        view.draws = draws;
        view.time = std::chrono::duration<float>(now - start).count();

        const bool lastFrame = options.maxFrames > 0 && renderer.frameNumber() + 1 >= options.maxFrames;
        if (lastFrame && options.screenshot) {
            renderer.requestScreenshot(*options.screenshot);
        }
        auto frame = renderer.beginFrame();
        if (!frame) {
            cpuProfiler.endFrame();
            SDL_Delay(5);
            continue;
        }
        const FrameInfo info = frame->info();
        RenderGraph graph(graphResources);
        RGImportedImage swapchainImage;
        swapchainImage.image = frame->image;
        swapchainImage.view = frame->imageView;
        swapchainImage.format = frame->format;
        swapchainImage.extent = frame->extent;
        swapchainImage.initial = usageState(ResourceUsage::ColorAttachment);
        swapchainImage.final = usageState(ResourceUsage::ColorAttachment);
        const RGImage output = graph.importImage("Swapchain", swapchainImage);
        scene.addToGraph(graph, info, frame->extent, view, output, frame->format, &lastStats);

        overlay.newFrame(frame->extent, dt);
        debug::ProfilerSnapshot snapshot;
        snapshot.cpu = &cpuProfiler;
        snapshot.gpu = &renderer.profiler();
        snapshot.cpuFrameMs = &cpuFrameMs;
        snapshot.gpuFrameMs = &gpuFrameMs;
        snapshot.render = lastStats;
        snapshot.memory = queryMemoryStats(renderer.context());
        snapshot.resolution = frame->extent;
        snapshot.frameNumber = frame->frameNumber;
        overlay.drawProfiler(snapshot);
        overlay.addToGraph(graph, output);

        graph.execute(info);
        renderer.endFrame(*frame);
        cpuProfiler.endFrame();
        cpuFrameMs.push(static_cast<float>(cpuProfiler.lastFrameMs()));
        if (renderer.profiler().enabled()) {
            gpuFrameMs.push(static_cast<float>(renderer.profiler().lastFrameMs()));
        }

        ++statsFrames;
        const double elapsed = std::chrono::duration<double>(now - statsStart).count();
        if (elapsed >= 2.0) {
            const VoxelWorldStats ws = world.stats();
            const StreamedMeshSet::Stats ms = chunkMeshes.stats();
            ZODAR_LOG_INFO("{:.1f} FPS | chunks {} (LOD0-3: {}/{}/{}/{}), {} pending, {:.1f} M tris, {:.0f} MB voxels, "
                           "{:.0f} MB meshes | draws {} culled {}",
                           statsFrames / elapsed, ms.meshes, ws.lodHistogram[0], ws.lodHistogram[1],
                           ws.lodHistogram[2], ws.lodHistogram[3], ws.generateInFlight + ws.meshInFlight,
                           double(ms.triangles) / 1e6, double(ws.voxelBytes) / 1048576.0,
                           double(ms.gpuBytes) / 1048576.0, lastStats.drawCalls, lastStats.culled);
            statsStart = now;
            statsFrames = 0;
        }
        if (lastFrame) break;
    }

    renderer.waitIdle();
    const StreamedMeshSet::Stats ms = chunkMeshes.stats();
    ZODAR_LOG_INFO("Rendered {} frames; {} chunk meshes, {} triangles, last frame {} draw calls ({} culled)",
                   renderer.frameNumber(), ms.meshes, ms.triangles, lastStats.drawCalls, lastStats.culled);
    if (renderer.context().validationEnabled() && renderer.context().validationErrorCount() > 0) {
        ZODAR_LOG_ERROR("{} Vulkan validation errors were reported", renderer.context().validationErrorCount());
        return 2;
    }
    if (options.screenshot && !renderer.lastScreenshotSucceeded()) return 3;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    zodar::log::init({spdlog::level::info, ""});
    int exitCode = EXIT_FAILURE;
    try {
        exitCode = run(parseOptions(argc, argv));
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
    }
    ZODAR_LOG_INFO("Shutdown complete (exit code {})", exitCode);
    zodar::log::shutdown();
    return exitCode;
}
