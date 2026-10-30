// ZodarBench: deterministic rendering benchmark that writes a JSON report.
//
//   ZodarBench [--scene grid|unique|city|model] [--model file.glb] [--objects N] [--blocks N] [--lights N]
//              [--width W] [--height H] [--warmup N] [--frames N] [--path graph|reference]
//              [--present immediate|low-latency|vsync] [--overlay] [--no-validation] [--sync-validation]
//              [--output report.json] [--screenshot out.ppm]
//
// Scenes:
//   grid    N instanced objects (2 meshes x 16 materials): GPU throughput, few draw calls.
//   unique  N objects, each with its own mesh and material: CPU cost per draw/state change.
//   city    B x B street blocks of buildings and props, street-level camera: most objects
//           are off-screen or hidden behind buildings (culling and overdraw).
//   model   a glTF file.
// The camera follows a fixed path over the measured frames (orbit; street drive for city),
// so every run renders the same frames. GPU times come from timestamp queries; on a software rasterizer (llvmpipe)
// they are reported but flagged and must not be read as GPU performance.
// Exit codes: 0 ok, 1 fatal error, 2 validation errors.

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

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace zodar;
using namespace zodar::gfx;

struct Options {
    std::string scene = "grid";
    std::optional<std::filesystem::path> model;
    uint32_t objects = 4096;
    uint32_t blocks = 32;
    uint32_t lights = 32;
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t warmup = 60;
    uint32_t frames = 600;
    bool referencePath = false;
    PresentMode present = PresentMode::Immediate;
    bool overlay = false;
    bool validation = true;
    bool syncValidation = false;
    std::filesystem::path output = "zodarbench.json";
    std::optional<std::filesystem::path> screenshot;
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) fail("Missing value for " + std::string(a));
            return argv[++i];
        };
        if (a == "--scene") o.scene = next();
        else if (a == "--model") { o.model = next(); o.scene = "model"; }
        else if (a == "--objects") o.objects = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--blocks") o.blocks = std::max(2u, static_cast<uint32_t>(std::stoul(next())));
        else if (a == "--lights") o.lights = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--width") o.width = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--height") o.height = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--warmup") o.warmup = static_cast<uint32_t>(std::stoul(next()));
        else if (a == "--frames") o.frames = std::max(1u, static_cast<uint32_t>(std::stoul(next())));
        else if (a == "--path") {
            const std::string p = next();
            if (p != "graph" && p != "reference") fail("--path must be graph or reference");
            o.referencePath = p == "reference";
        } else if (a == "--present") {
            const std::string p = next();
            o.present = p == "vsync" ? PresentMode::VSync : p == "low-latency" ? PresentMode::LowLatency : PresentMode::Immediate;
        } else if (a == "--overlay") o.overlay = true;
        else if (a == "--no-validation") o.validation = false;
        else if (a == "--sync-validation") o.syncValidation = true;
        else if (a == "--output") o.output = next();
        else if (a == "--screenshot") o.screenshot = next();
        else fail("Unknown argument '" + std::string(a) + "'");
    }
    if (o.scene == "model" && !o.model) fail("--scene model needs --model <file>");
    if (o.scene != "grid" && o.scene != "unique" && o.scene != "city" && o.scene != "model") {
        fail("Unknown scene '" + o.scene + "'");
    }
    return o;
}

struct Stats {
    std::vector<double> samples;
    void add(double v) { samples.push_back(v); }
    double avg() const {
        double s = 0;
        for (double v : samples) s += v;
        return samples.empty() ? 0.0 : s / static_cast<double>(samples.size());
    }
    double pct(double p) const {
        if (samples.empty()) return 0.0;
        std::vector<double> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        return sorted[std::min(sorted.size() - 1, static_cast<size_t>(p * static_cast<double>(sorted.size() - 1) + 0.5))];
    }
    std::string json() const {
        std::ostringstream s;
        s.precision(4);
        s << std::fixed << "{\"avg\": " << avg() << ", \"p50\": " << pct(0.5) << ", \"p95\": " << pct(0.95)
          << ", \"p99\": " << pct(0.99) << ", \"max\": " << pct(1.0) << "}";
        return s.str();
    }
};

// Deterministic hash -> [0,1) so every run builds the same scene.
float hash01(uint32_t a, uint32_t b = 0) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

std::string escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

int run(const Options& o) {
    Platform platform;
    Window window({"ZodarBench", o.width, o.height, false});
    Renderer renderer(window, {"ZodarBench", o.validation, o.present,
                               Platform::userDataPath("ZODAR", "ZodarBench") / "pipeline_cache.bin", o.syncValidation});
    UploadContext upload(renderer.context());
    const char* base = SDL_GetBasePath();
    SceneRenderer scene(renderer.context(), upload,
                        {std::filesystem::path(base ? base : "") / "shaders", VK_FORMAT_R16G16B16A16_SFLOAT,
                         VK_FORMAT_D32_SFLOAT, Renderer::kFramesInFlight});
    RenderGraphResources graphResources(renderer.context(), scene.bindless());
    std::unique_ptr<debug::DebugOverlay> overlay;
    if (o.overlay) {
        overlay = std::make_unique<debug::DebugOverlay>(renderer.context(), &window, renderer.swapchain().format(),
                                                        renderer.swapchain().imageCount());
    }

    // --- Scene ---------------------------------------------------------------------
    std::vector<GpuMesh> meshes;
    std::unique_ptr<Model> model;
    std::vector<MeshDraw> draws;
    Bounds bounds;
    SceneView view;
    uint32_t uniqueMeshes = 2;
    uint32_t uniqueMaterials = 16;
    float cityHalf = 0.0f;
    {
        UploadBatch batch(upload);
        if (o.scene == "grid") {
            meshes.reserve(3);
            meshes.emplace_back(batch, primitives::cube(0.8f));
            meshes.emplace_back(batch, primitives::uvSphere(0.45f, 32, 16));
            meshes.emplace_back(batch, primitives::plane(1.0f, 1.0f));
            std::vector<MaterialHandle> materials;
            for (int i = 0; i < 16; ++i) {
                MaterialDesc d;
                d.baseColorFactor = glm::vec4(0.3f + 0.04f * i, 0.5f, 0.9f - 0.04f * i, 1.0f);
                d.metallic = (i % 4 == 0) ? 1.0f : 0.0f;
                d.roughness = 0.2f + 0.05f * static_cast<float>(i);
                materials.push_back(scene.materials().createMaterial(d));
            }
            const auto side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(o.objects))));
            for (uint32_t i = 0; i < o.objects; ++i) {
                const float x = (static_cast<float>(i % side) - side * 0.5f) * 1.2f;
                const float z = (static_cast<float>(i / side) - side * 0.5f) * 1.2f;
                const float y = 0.5f + 0.4f * std::sin(x * 0.3f) * std::cos(z * 0.3f);
                draws.push_back({&meshes[i % 2], 0, materials[(i * 7) % materials.size()],
                                 glm::translate(glm::mat4(1), {x, y, z})});
            }
            MaterialDesc ground;
            ground.baseColorFactor = {0.35f, 0.35f, 0.37f, 1};
            ground.roughness = 0.9f;
            const float extent = side * 1.2f + 4.0f;
            draws.push_back({&meshes[2], 0, scene.materials().createMaterial(ground),
                             glm::scale(glm::mat4(1), glm::vec3(extent, 1, extent))});
        } else if (o.scene == "unique") {
            // Every object: its own deformed mesh and its own material (no instancing possible).
            const uint32_t materialCount = std::min(o.objects, MaterialSystem::kMaxMaterials - 64);
            std::vector<MaterialHandle> materials;
            for (uint32_t i = 0; i < materialCount; ++i) {
                MaterialDesc d;
                d.baseColorFactor = glm::vec4(hash01(i, 1), hash01(i, 2), hash01(i, 3), 1.0f);
                d.metallic = hash01(i, 4) < 0.25f ? 1.0f : 0.0f;
                d.roughness = 0.15f + 0.8f * hash01(i, 5);
                materials.push_back(scene.materials().createMaterial(d));
            }
            meshes.reserve(o.objects + 1);
            for (uint32_t i = 0; i < o.objects; ++i) {
                MeshData mesh = (i % 2 == 0)
                    ? primitives::uvSphere(0.45f, 8 + static_cast<uint32_t>(hash01(i, 6) * 16), 6 + static_cast<uint32_t>(hash01(i, 7) * 10))
                    : primitives::cube(0.8f);
                for (size_t v = 0; v < mesh.vertices.size(); ++v) {
                    mesh.vertices[v].position *= 0.85f + 0.3f * hash01(i, 100 + static_cast<uint32_t>(v));
                }
                mesh.generateNormals();
                mesh.generateTangents();
                mesh.computeBounds();
                mesh.name = "Unique" + std::to_string(i);
                meshes.emplace_back(batch, mesh);
            }
            const auto side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(o.objects))));
            for (uint32_t i = 0; i < o.objects; ++i) {
                const float x = (static_cast<float>(i % side) - side * 0.5f) * 1.2f;
                const float z = (static_cast<float>(i / side) - side * 0.5f) * 1.2f;
                draws.push_back({&meshes[i], 0, materials[i % materials.size()], glm::translate(glm::mat4(1), {x, 0.5f, z})});
            }
            uniqueMeshes = o.objects;
            uniqueMaterials = materialCount;
            meshes.emplace_back(batch, primitives::plane(1.0f, 1.0f));
            MaterialDesc ground;
            ground.baseColorFactor = {0.35f, 0.35f, 0.37f, 1};
            ground.roughness = 0.9f;
            draws.push_back({&meshes.back(), 0, scene.materials().createMaterial(ground),
                             glm::scale(glm::mat4(1), glm::vec3(side * 1.2f + 4.0f, 1, side * 1.2f + 4.0f))});
        } else if (o.scene == "city") {
            // Street grid: each block holds 2x2 buildings and a ring of props on the sidewalk.
            meshes.reserve(3);
            meshes.emplace_back(batch, primitives::cube(1.0f));
            meshes.emplace_back(batch, primitives::uvSphere(0.5f, 16, 8));
            meshes.emplace_back(batch, primitives::plane(1.0f, 1.0f));
            std::vector<MaterialHandle> facades, props;
            for (uint32_t i = 0; i < 48; ++i) {
                MaterialDesc d;
                const float g = 0.25f + 0.5f * hash01(i, 11);
                d.baseColorFactor = glm::vec4(g * (0.9f + 0.2f * hash01(i, 12)), g, g * (0.9f + 0.2f * hash01(i, 13)), 1);
                d.roughness = 0.6f + 0.35f * hash01(i, 14);
                facades.push_back(scene.materials().createMaterial(d));
            }
            for (uint32_t i = 0; i < 8; ++i) {
                MaterialDesc d;
                d.baseColorFactor = glm::vec4(hash01(i, 21), hash01(i, 22), hash01(i, 23), 1);
                d.metallic = i % 2 == 0 ? 1.0f : 0.0f;
                d.roughness = 0.3f + 0.5f * hash01(i, 24);
                props.push_back(scene.materials().createMaterial(d));
            }
            const float block = 40.0f, street = 10.0f, pitch = block + street;
            const float half = o.blocks * pitch * 0.5f;
            uint32_t id = 0;
            for (uint32_t bz = 0; bz < o.blocks; ++bz) {
                for (uint32_t bx = 0; bx < o.blocks; ++bx) {
                    const glm::vec2 origin{bx * pitch - half + street * 0.5f, bz * pitch - half + street * 0.5f};
                    for (uint32_t k = 0; k < 4; ++k, ++id) {
                        const float w = 15.0f + 4.0f * hash01(id, 31), d = 15.0f + 4.0f * hash01(id, 32);
                        const float h = 8.0f + 52.0f * hash01(id, 33) * hash01(id, 34);
                        const glm::vec3 centre{origin.x + (k % 2 + 0.5f) * block * 0.5f, h * 0.5f, origin.y + (k / 2 + 0.5f) * block * 0.5f};
                        draws.push_back({&meshes[0], 0, facades[id % facades.size()],
                                         glm::scale(glm::translate(glm::mat4(1), centre), {w, h, d})});
                    }
                    for (uint32_t p = 0; p < 16; ++p, ++id) {
                        const float along = (p % 4 + 0.5f) * block / 4.0f;
                        const glm::vec2 pos = p < 4 ? glm::vec2{along, 1.0f} : p < 8 ? glm::vec2{along, block - 1.0f}
                                            : p < 12 ? glm::vec2{1.0f, along} : glm::vec2{block - 1.0f, along};
                        const bool lamp = p % 2 == 0;
                        const glm::vec3 scale = lamp ? glm::vec3(0.25f, 4.5f, 0.25f) : glm::vec3(0.9f);
                        draws.push_back({&meshes[lamp ? 0 : 1], 0, props[id % props.size()],
                                         glm::scale(glm::translate(glm::mat4(1), {origin.x + pos.x, scale.y * 0.5f, origin.y + pos.y}), scale)});
                    }
                }
            }
            MaterialDesc asphalt;
            asphalt.baseColorFactor = {0.12f, 0.12f, 0.13f, 1};
            asphalt.roughness = 0.95f;
            draws.push_back({&meshes[2], 0, scene.materials().createMaterial(asphalt),
                             glm::scale(glm::mat4(1), glm::vec3(half * 2.0f + 40.0f, 1, half * 2.0f + 40.0f))});
            uniqueMaterials = 57;
            uniqueMeshes = 3;
            cityHalf = half;
        } else {
            const ModelData data = loadGltf(*o.model);
            model = std::make_unique<Model>(batch, scene, data);
            model->appendDraws(draws);
            uniqueMeshes = static_cast<uint32_t>(model->meshCount());
            uniqueMaterials = static_cast<uint32_t>(model->materials().size());
        }
        batch.submit();
    }
    for (const auto& d : draws) bounds.expand(d.mesh->bounds().transformed(d.transform));
    const glm::vec3 centre = bounds.center();
    const float radius = std::max(0.5f * glm::length(bounds.extent()), 0.1f);
    view.clearColor = {0.05f, 0.06f, 0.08f, 1};
    view.lighting.ambientIntensity = 0.2f;
    view.lighting.sun = {{-0.4f, -1.0f, -0.3f}, {1.0f, 0.95f, 0.9f}, 2.5f, true};
    const uint32_t lightCount = std::min(o.lights, SceneRenderer::kMaxPointLights);
    for (uint32_t i = 0; i < lightCount; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(std::max(1u, o.lights));
        if (o.scene == "city") {
            // Street lights along the driven street.
            const float z = -cityHalf + (static_cast<float>(i) + 0.5f) * (2.0f * cityHalf / static_cast<float>(lightCount));
            view.lighting.pointLights.push_back({{(i % 2 ? 3.5f : -3.5f), 4.5f, z}, {1.0f, 0.8f, 0.55f}, 60.0f, 25.0f});
            continue;
        }
        view.lighting.pointLights.push_back({centre + glm::vec3(std::cos(a), 0.3f, std::sin(a)) * radius * 0.6f,
                                             glm::vec3(0.5f + 0.5f * std::cos(a), 0.6f, 0.5f + 0.5f * std::sin(a)),
                                             radius * 2.0f, radius * 0.5f});
    }
    if (o.scene == "city") {
        view.lighting.sun = {{-0.3f, -1.0f, -0.5f}, {1.0f, 0.9f, 0.8f}, 2.0f, true};
        view.camera.projection.nearPlane = 0.1f;
    }
    if (o.scene != "city") view.camera.projection.nearPlane = std::max(0.01f, radius * 0.001f);
    view.draws = draws;
    ZODAR_LOG_INFO("Benchmark scene '{}': {} draws, path {}", o.scene, draws.size(), o.referencePath ? "reference" : "graph");

    // --- Frame loop ------------------------------------------------------------------
    CpuProfiler cpu;
    Stats cpuFrame, gpuFrame;
    std::map<std::string, Stats> gpuPasses, cpuScopes;
    Stats drawCalls, visible, culled, triangles; // Per measured frame (the view changes along the path).
    RenderStats lastStats;
    const uint32_t total = o.warmup + o.frames;
    auto last = std::chrono::steady_clock::now();
    uint32_t rendered = 0;
    while (rendered < total && !window.shouldClose()) {
        window.pollEvents([&](const SDL_Event& e) {
            if (overlay) overlay->processEvent(e);
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) window.requestClose();
        });
        const bool measuring = rendered >= o.warmup;
        const float t = measuring ? static_cast<float>(rendered - o.warmup) / static_cast<float>(o.frames) : 0.0f;
        const float angle = glm::two_pi<float>() * t;
        if (o.scene == "city") {
            // Drive down the central street (x = 0 lies in a street) at eye height.
            const float z = -cityHalf * 0.9f + t * cityHalf * 1.8f;
            view.camera.position = {0.0f, 1.7f, z};
            view.camera.setYawPitch(glm::pi<float>() + 0.35f * std::sin(angle * 3.0f), -0.03f);
        } else {
            view.camera.position = centre + glm::vec3(std::sin(angle), 0.45f, std::cos(angle)) * radius * 1.1f;
            view.camera.lookAt(centre);
        }

        // Screenshot from the middle of the measured path (representative view).
        if (rendered == o.warmup + o.frames / 2 && o.screenshot) renderer.requestScreenshot(*o.screenshot);
        cpu.beginFrame();
        const uint32_t waitScope = cpu.begin("Wait + acquire");
        auto frame = renderer.beginFrame();
        cpu.end(waitScope);
        if (!frame) {
            cpu.endFrame();
            continue;
        }
        const FrameInfo info = frame->info();
        if (o.referencePath) {
            const uint32_t s = cpu.begin("Prepare + record");
            lastStats = scene.render(info, frame->extent, view);
            scene.resolve(info, frame->imageView, frame->format, frame->extent);
            cpu.end(s);
        } else {
            const uint32_t s1 = cpu.begin("Prepare + build graph");
            RenderGraph graph(graphResources);
            RGImportedImage image;
            image.image = frame->image;
            image.view = frame->imageView;
            image.format = frame->format;
            image.extent = frame->extent;
            image.initial = usageState(ResourceUsage::ColorAttachment);
            image.final = usageState(ResourceUsage::ColorAttachment);
            const RGImage output = graph.importImage("Swapchain", image);
            scene.addToGraph(graph, info, frame->extent, view, output, frame->format, &lastStats);
            if (overlay) {
                const auto now = std::chrono::steady_clock::now();
                overlay->newFrame(frame->extent, std::chrono::duration<float>(now - last).count());
                debug::ProfilerSnapshot snap;
                snap.cpu = &cpu;
                snap.gpu = &renderer.profiler();
                snap.render = lastStats;
                snap.memory = queryMemoryStats(renderer.context());
                snap.resolution = frame->extent;
                snap.frameNumber = frame->frameNumber;
                overlay->drawProfiler(snap);
                overlay->addToGraph(graph, output);
            }
            cpu.end(s1);
            const uint32_t s2 = cpu.begin("Record");
            graph.execute(info);
            cpu.end(s2);
        }
        const uint32_t s3 = cpu.begin("Submit + present");
        renderer.endFrame(*frame);
        cpu.end(s3);
        cpu.endFrame();
        last = std::chrono::steady_clock::now();

        if (measuring) {
            drawCalls.add(lastStats.drawCalls);
            visible.add(lastStats.instances);
            culled.add(lastStats.culled);
            triangles.add(static_cast<double>(lastStats.triangles));
            cpuFrame.add(cpu.lastFrameMs());
            for (const auto& s : cpu.lastFrame()) cpuScopes[s.name].add(s.ms);
            // GPU results lag by the frames in flight; they belong to measured frames once
            // the lag has passed the warm-up.
            if (renderer.profiler().enabled() && rendered >= o.warmup + Renderer::kFramesInFlight) {
                gpuFrame.add(renderer.profiler().lastFrameMs());
                for (const auto& s : renderer.profiler().lastResults()) gpuPasses[s.name].add(s.ms);
            }
        }
        ++rendered;
    }
    renderer.waitIdle();

    // --- Report ----------------------------------------------------------------------
    const auto& ctx = renderer.context();
    const auto& info = ctx.deviceInfo();
    const GpuMemoryStats memory = queryMemoryStats(ctx);
    std::ostringstream j;
    j.precision(3);
    j << std::fixed << "{\n";
    j << "  \"tool\": \"ZodarBench\", \"engineVersion\": \"" << ZODAR_VERSION_MAJOR << "." << ZODAR_VERSION_MINOR << "."
      << ZODAR_VERSION_PATCH << "\",\n";
    j << "  \"device\": \"" << escape(info.name) << "\", \"driver\": \"" << escape(info.driverName) << " "
      << escape(info.driverInfo) << "\",\n";
    j << "  \"softwareRasterizer\": " << (info.isSoftwareRasterizer ? "true" : "false") << ",\n";
    j << "  \"note\": \""
      << (info.isSoftwareRasterizer ? "Software rasterizer: GPU times are NOT GPU performance." : "Hardware GPU.")
      << "\",\n";
    j << "  \"scene\": \"" << o.scene << "\", \"renderPath\": \"" << (o.referencePath ? "reference" : "graph")
      << "\", \"overlay\": " << (o.overlay ? "true" : "false") << ",\n";
    j << "  \"sceneDetails\": {\"objects\": " << draws.size() << ", \"uniqueMeshes\": " << uniqueMeshes
      << ", \"uniqueMaterials\": " << uniqueMaterials << ", \"pointLights\": " << view.lighting.pointLights.size()
      << (o.scene == "city" ? ", \"blocks\": " + std::to_string(o.blocks) : std::string()) << "},\n";
    j << "  \"resolution\": [" << renderer.swapchain().extent().width << ", " << renderer.swapchain().extent().height
      << "], \"presentMode\": \"" << toString(o.present) << "\",\n";
    j << "  \"warmupFrames\": " << o.warmup << ", \"measuredFrames\": " << cpuFrame.samples.size() << ",\n";
    j << "  \"cpuFrameMs\": " << cpuFrame.json() << ",\n";
    j << "  \"gpuFrameMs\": " << (gpuFrame.samples.empty() ? "null" : gpuFrame.json()) << ",\n";
    j << "  \"gpuPassesMs\": {";
    bool first = true;
    for (const auto& [name, st] : gpuPasses) {
        j << (first ? "\n" : ",\n") << "    \"" << escape(name) << "\": " << st.json();
        first = false;
    }
    j << "\n  },\n  \"cpuScopesMs\": {";
    first = true;
    for (const auto& [name, st] : cpuScopes) {
        j << (first ? "\n" : ",\n") << "    \"" << escape(name) << "\": " << st.json();
        first = false;
    }
    j << "\n  },\n";
    j << "  \"perFrame\": {\"drawCalls\": " << drawCalls.json() << ", \"visibleObjects\": " << visible.json()
      << ", \"culledObjects\": " << culled.json() << ", \"triangles\": " << triangles.json() << "},\n";
    j << "  \"lastFrame\": {\"drawCalls\": " << lastStats.drawCalls << ", \"visibleObjects\": " << lastStats.instances
      << ", \"submittedObjects\": " << lastStats.submitted << ", \"culledObjects\": " << lastStats.culled
      << ", \"triangles\": " << lastStats.triangles << "},\n";
    j << "  \"memory\": {\"deviceLocalUsageMiB\": " << memory.deviceLocalUsage / 1048576.0
      << ", \"deviceLocalBudgetMiB\": " << memory.deviceLocalBudget / 1048576.0
      << ", \"allocationMiB\": " << memory.allocationBytes / 1048576.0
      << ", \"allocations\": " << memory.allocationCount << ", \"budgetFromDriver\": "
      << (memory.budgetFromDriver ? "true" : "false") << "},\n";
    j << "  \"validation\": {\"enabled\": " << (ctx.validationEnabled() ? "true" : "false")
      << ", \"errors\": " << ctx.validationErrorCount() << ", \"warnings\": " << ctx.validationWarningCount() << "}\n";
    j << "}\n";
    std::ofstream(o.output) << j.str();
    ZODAR_LOG_INFO("Report written to {}", o.output.string());
    ZODAR_LOG_INFO("CPU frame avg {:.3f} ms | GPU frame avg {:.3f} ms | {} draws, {} visible, {} triangles",
                   cpuFrame.avg(), gpuFrame.avg(), lastStats.drawCalls, lastStats.instances, lastStats.triangles);
    if (info.isSoftwareRasterizer) {
        ZODAR_LOG_WARN("Software rasterizer: these numbers are not GPU performance");
    }
    return ctx.validationEnabled() && ctx.validationErrorCount() > 0 ? 2 : 0;
}

} // namespace

int main(int argc, char** argv) {
    zodar::log::init({spdlog::level::info, ""});
    int code = EXIT_FAILURE;
    try {
        code = run(parse(argc, argv));
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
    }
    zodar::log::shutdown();
    return code;
}
