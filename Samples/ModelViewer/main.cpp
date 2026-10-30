// Milestone 2 sample: glTF viewer / material demo on the ZODAR scene renderer.
//
//   ModelViewer [model.gltf|.glb] [--frames N] [--screenshot out.ppm] [--no-validation] [--sync-validation]
//               [--width W] [--height H] [--yaw DEG] [--pitch DEG] [--zoom F] [--spin DEG_PER_S]
//               [--nearest] [--reference-path] [--no-overlay]
//
// Without a model path a built-in demo scene of procedural meshes is shown.
// Controls: left mouse drag orbits, mouse wheel zooms, F1 toggles the profiler, Esc quits.
// Exit codes: 0 ok, 1 fatal error, 2 validation errors, 3 screenshot failed.

#include "Assets/ModelData.h"
#include "Assets/Primitives.h"
#include "Core/Error.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Debug/DebugOverlay.h"
#include "Platform/Platform.h"
#include "Platform/Window.h"
#include "Renderer/GpuProfiler.h"
#include "Renderer/Mesh.h"
#include "Renderer/Model.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace zodar;
using namespace zodar::gfx;

struct Options {
    std::optional<std::filesystem::path> model;
    uint64_t maxFrames = 0;
    std::optional<std::filesystem::path> screenshot;
    bool validation = true;
    uint32_t width = 1280;
    uint32_t height = 720;
    float yaw = 30.0f;
    float pitch = -15.0f;
    float zoom = 1.0f;
    float spin = 0.0f;
    bool nearest = false;
    bool referencePath = false; // Pre-render-graph path, kept for comparison.
    bool overlay = true;
    bool syncValidation = false;
};

Options parseOptions(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) fail("Missing value for " + std::string(arg));
            return argv[++i];
        };
        if (arg == "--frames") o.maxFrames = std::stoull(next());
        else if (arg == "--screenshot") o.screenshot = next();
        else if (arg == "--no-validation") o.validation = false;
        else if (arg == "--sync-validation") o.syncValidation = true;
        else if (arg == "--width") o.width = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "--height") o.height = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "--yaw") o.yaw = std::stof(next());
        else if (arg == "--pitch") o.pitch = std::stof(next());
        else if (arg == "--zoom") o.zoom = std::stof(next());
        else if (arg == "--spin") o.spin = std::stof(next());
        else if (arg == "--nearest") o.nearest = true;
        else if (arg == "--reference-path") o.referencePath = true;
        else if (arg == "--no-overlay") o.overlay = false;
        else if (!arg.starts_with("--") && !o.model) o.model = std::string(arg);
        else fail("Unknown argument '" + std::string(arg) + "'");
    }
    return o;
}

// Procedural material showcase: a grid of spheres sweeping roughness and metalness,
// cubes and a ground plane. Only engine primitives and built-in shaders are used.
struct DemoScene {
    std::vector<GpuMesh> meshes;
    std::vector<MeshDraw> draws;
    Bounds bounds;

    DemoScene(UploadBatch& batch, SceneRenderer& scene) {
        meshes.reserve(3);
        meshes.emplace_back(batch, primitives::uvSphere(0.4f, 64, 32));
        meshes.emplace_back(batch, primitives::cube(0.8f));
        meshes.emplace_back(batch, primitives::plane(12.0f, 8.0f));
        MaterialSystem& m = scene.materials();

        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 6; ++col) {
                MaterialDesc d;
                d.name = "Sphere";
                d.baseColorFactor = row == 0 ? glm::vec4(0.9f, 0.25f, 0.2f, 1) : glm::vec4(0.95f, 0.75f, 0.35f, 1);
                d.metallic = static_cast<float>(row);
                d.roughness = 0.08f + 0.92f * static_cast<float>(col) / 5.0f;
                draws.push_back({&meshes[0], 0, m.createMaterial(d),
                                 glm::translate(glm::mat4(1), {col * 1.0f - 2.5f, 0.45f + row * 1.0f, 0.0f})});
            }
        }
        MaterialDesc concrete;
        concrete.baseColorFactor = {0.45f, 0.45f, 0.47f, 1};
        concrete.roughness = 0.9f;
        draws.push_back({&meshes[2], 0, m.createMaterial(concrete), glm::mat4(1)});

        MaterialDesc glow;
        glow.shader = m.unlitShader();
        glow.baseColorFactor = {0.1f, 0.6f, 1.0f, 1};
        draws.push_back({&meshes[1], 0, m.createMaterial(glow), glm::translate(glm::mat4(1), {-4.0f, 0.4f, 1.5f})});

        MaterialDesc glass;
        glass.alphaMode = AlphaMode::Blend;
        glass.baseColorFactor = {0.3f, 0.9f, 0.4f, 0.4f};
        glass.roughness = 0.1f;
        draws.push_back({&meshes[1], 0, m.createMaterial(glass), glm::translate(glm::mat4(1), {4.0f, 0.4f, 1.5f})});

        for (const auto& d : draws) {
            bounds.expand(d.mesh->bounds().transformed(d.transform));
        }
    }
};

int run(const Options& options) {
    Platform platform;
    Window window({"ZODAR Engine - Model Viewer", options.width, options.height, true});
    Renderer renderer(window, {"ModelViewer", options.validation, PresentMode::VSync,
                               Platform::userDataPath("ZODAR", "ModelViewer") / "pipeline_cache.bin", options.syncValidation});
    UploadContext upload(renderer.context());

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
    ZODAR_LOG_INFO("Render path: {}", options.referencePath ? "reference (hand-written barriers)" : "render graph");

    std::unique_ptr<Model> model;
    std::unique_ptr<DemoScene> demo;
    std::vector<MeshDraw> draws;
    Bounds bounds;
    {
        const auto start = std::chrono::steady_clock::now();
        UploadBatch batch(upload);
        if (options.model) {
            const ModelData data = loadGltf(*options.model);
            ModelUploadOptions uploadOptions;
            if (options.nearest) {
                uploadOptions.samplerOverride = SamplerDesc::nearest();
            }
            model = std::make_unique<Model>(batch, scene, data, uploadOptions);
            model->appendDraws(draws);
            bounds = model->bounds();
            ZODAR_LOG_INFO("Loaded '{}': {} meshes, {} textures, {} materials, {} draws", data.name, model->meshCount(),
                           model->textureCount(), model->materials().size(), draws.size());
        } else {
            demo = std::make_unique<DemoScene>(batch, scene);
            draws = demo->draws;
            bounds = demo->bounds;
        }
        batch.submit();
        ZODAR_LOG_INFO("Scene ready in {:.0f} ms",
                       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }

    SceneView view;
    view.clearColor = {0.06f, 0.07f, 0.09f, 1.0f};
    view.lighting.ambientIntensity = 0.25f;
    view.lighting.ambientColor = {0.75f, 0.85f, 1.0f};
    view.lighting.sun = {{-0.5f, -1.0f, -0.6f}, {1.0f, 0.96f, 0.9f}, 3.0f, true};
    view.camera.projection.nearPlane = std::max(0.01f, glm::length(bounds.extent()) * 0.001f);

    const glm::vec3 target = bounds.center();
    const float radius = std::max(0.5f * glm::length(bounds.extent()), 0.01f);
    float yaw = glm::radians(options.yaw);
    float pitch = glm::radians(options.pitch);
    float distance = radius / std::sin(view.camera.projection.verticalFovRadians * 0.5f) / options.zoom;
    view.lighting.pointLights.push_back({target + glm::vec3(radius, radius * 0.8f, radius), {1.0f, 0.75f, 0.5f},
                                         radius * radius * 4.0f, radius * 6.0f});

    bool dragging = false;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start;
    auto statsStart = start;
    uint64_t statsFrames = 0;
    RenderStats lastStats;

    while (!window.shouldClose()) {
        cpuProfiler.beginFrame();
        const uint32_t eventsScope = cpuProfiler.begin("Events");
        window.pollEvents([&](const SDL_Event& e) {
            if (overlay.processEvent(e)) return;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) window.requestClose();
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) dragging = true;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) dragging = false;
            if (e.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                yaw -= e.motion.xrel * 0.005f;
                pitch = std::clamp(pitch - e.motion.yrel * 0.005f, -1.5f, 1.5f);
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) distance *= e.wheel.y > 0 ? 0.9f : 1.1f;
        });
        cpuProfiler.end(eventsScope);

        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        yaw += glm::radians(options.spin) * dt;

        const glm::vec3 offset{std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
        view.camera.position = target + offset * distance;
        view.camera.lookAt(target);
        view.time = std::chrono::duration<float>(now - start).count();
        view.draws = draws;

        const bool lastFrame = options.maxFrames > 0 && renderer.frameNumber() + 1 >= options.maxFrames;
        if (lastFrame && options.screenshot) {
            renderer.requestScreenshot(*options.screenshot);
        }
        const uint32_t acquireScope = cpuProfiler.begin("Wait + acquire");
        auto frame = renderer.beginFrame();
        cpuProfiler.end(acquireScope);
        if (!frame) {
            cpuProfiler.endFrame();
            SDL_Delay(5);
            continue;
        }
        const FrameInfo info = frame->info();
        if (options.referencePath) {
            lastStats = scene.render(info, frame->extent, view);
            scene.resolve(info, frame->imageView, frame->format, frame->extent);
        } else {
            const uint32_t buildScope = cpuProfiler.begin("Build graph + prepare");
            RenderGraph graph(graphResources);
            // The frame loop hands over the swapchain image as a colour attachment and
            // transitions it for presentation afterwards.
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
            cpuProfiler.end(buildScope);

            const uint32_t recordScope = cpuProfiler.begin("Record");
            graph.execute(info);
            cpuProfiler.end(recordScope);
        }
        const uint32_t submitScope = cpuProfiler.begin("Submit + present");
        renderer.endFrame(*frame);
        cpuProfiler.end(submitScope);
        cpuProfiler.endFrame();
        cpuFrameMs.push(static_cast<float>(cpuProfiler.lastFrameMs()));
        if (renderer.profiler().enabled()) {
            gpuFrameMs.push(static_cast<float>(renderer.profiler().lastFrameMs()));
        }

        ++statsFrames;
        const double elapsed = std::chrono::duration<double>(now - statsStart).count();
        if (elapsed >= 2.0) {
            ZODAR_LOG_INFO("{:.1f} FPS | draws {} (instances {}, culled {})", statsFrames / elapsed,
                           lastStats.drawCalls, lastStats.instances, lastStats.culled);
            statsStart = now;
            statsFrames = 0;
        }
        if (lastFrame) break;
    }

    renderer.waitIdle();
    ZODAR_LOG_INFO("Rendered {} frames; last frame: {} draw calls, {} instances, {} culled", renderer.frameNumber(),
                   lastStats.drawCalls, lastStats.instances, lastStats.culled);
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
