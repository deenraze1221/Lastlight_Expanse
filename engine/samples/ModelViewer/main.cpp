// Milestone 2 sample: glTF viewer / material demo on the ZODAR scene renderer, built on
// zodar::Application (standard options: --help).
//
//   ModelViewer [standard options] [model.gltf|.glb] [--yaw DEG] [--pitch DEG] [--zoom F] [--spin DEG_PER_S]
//               [--nearest] [--reference-path]
//               [--environment sky|none|file.hdr] [--lighting-quality low|medium|high|ultra]
//               [--debug-view none|light-count|cluster-slice|normals|direct|indirect|specular-ibl]
//               [--lights N] [--no-prepass]
//
// Without a model path a built-in demo scene of procedural meshes is shown. The environment
// (default: procedural sky) provides image-based lighting and the background.
// Camera: orbit around the scene (--yaw/--pitch/--zoom/--spin); the standard --camera option
// places a fixed camera instead. Spin and light animation use the runtime clock, so
// --deterministic runs render the same frames.
// Controls: left mouse drag orbits, mouse wheel zooms, F1 toggles the profiler, F2 cycles the
// lighting debug views, Esc quits.
// Exit codes: standard (0 ok, 1 fatal error, 2 validation errors, 3 screenshot failed).

#include "Assets/EnvironmentMaps.h"
#include "Assets/ModelData.h"
#include "Assets/Primitives.h"
#include "Core/Error.h"
#include "Core/Log.h"
#include "Debug/DebugOverlay.h"
#include "Renderer/Mesh.h"
#include "Renderer/Model.h"
#include "Renderer/Resources/UploadContext.h"
#include "Runtime/Application.h"
#include "Runtime/CommandLine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace zodar;
using namespace zodar::gfx;

constexpr std::string_view kUsage =
    "ModelViewer [standard options] [model.gltf|.glb] [--yaw DEG] [--pitch DEG] [--zoom F] [--spin DEG_PER_S]\n"
    "            [--nearest] [--reference-path] [--environment sky|none|file.hdr]\n"
    "            [--lighting-quality low|medium|high|ultra] [--debug-view NAME] [--lights N] [--no-prepass]";

// Viewer-only options; window, frames, screenshots, validation etc. are in ApplicationDesc.
struct Options {
    std::optional<std::filesystem::path> model;
    float yaw = 30.0f;
    float pitch = -15.0f;
    float zoom = 1.0f;
    float spin = 0.0f;
    bool nearest = false;
    bool referencePath = false; // Pre-render-graph path, kept for comparison.
    std::string environment = "sky";
    LightingQuality quality = LightingQuality::High;
    LightingDebugView debugView = LightingDebugView::None;
    uint32_t extraLights = 0; // Animated coloured point lights around the scene
    bool prepass = true;
};

Options parseOptions(std::span<const std::string> args) {
    Options o;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= args.size()) fail("Missing value for " + std::string(arg));
            return args[++i];
        };
        const auto count = [&]() {
            const int64_t v = parseIntArgument(arg, next());
            if (v < 0 || v > 100000) fail(std::string(arg) + " must be in [0, 100000]");
            return static_cast<uint32_t>(v);
        };
        const auto number = [&]() { return static_cast<float>(parseFloatArgument(arg, next())); };
        if (arg == "--environment") o.environment = next();
        else if (arg == "--lighting-quality") o.quality = parseLightingQuality(next());
        else if (arg == "--debug-view") o.debugView = parseLightingDebugView(next());
        else if (arg == "--lights") o.extraLights = count();
        else if (arg == "--no-prepass") o.prepass = false;
        else if (arg == "--yaw") o.yaw = number();
        else if (arg == "--pitch") o.pitch = number();
        else if (arg == "--zoom") o.zoom = number();
        else if (arg == "--spin") o.spin = number();
        else if (arg == "--nearest") o.nearest = true;
        else if (arg == "--reference-path") o.referencePath = true;
        else if (!arg.starts_with("--") && !o.model) o.model = std::string(arg);
        else fail("Unknown argument '" + std::string(arg) + "' (see --help)");
    }
    if (!(o.zoom > 0.0f)) fail("--zoom must be > 0");
    return o;
}

ApplicationDesc defaultDesc() {
    ApplicationDesc desc;
    desc.name = "ZODAR Engine - Model Viewer";
    desc.identifier = "ModelViewer";
    desc.overlay = true;
    return desc;
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

class ViewerApp final : public Application {
public:
    ViewerApp(const ApplicationDesc& desc, Options options) : Application(desc), m_options(std::move(options)) {}

protected:
    void onStart() override {
        ZODAR_LOG_INFO("Render path: {}",
                       m_options.referencePath ? "reference (hand-written barriers)" : "render graph");
        // The reference path records outside the render graph and never drew the overlay.
        if (m_options.referencePath) overlay().visible = false;
        {
            const auto start = std::chrono::steady_clock::now();
            UploadBatch batch(upload());
            if (m_options.model) {
                const ModelData data = loadGltf(*m_options.model);
                ModelUploadOptions uploadOptions;
                if (m_options.nearest) {
                    uploadOptions.samplerOverride = SamplerDesc::nearest();
                }
                m_model = std::make_unique<Model>(batch, scene(), data, uploadOptions);
                m_model->appendDraws(m_draws);
                m_bounds = m_model->bounds();
                ZODAR_LOG_INFO("Loaded '{}': {} meshes, {} textures, {} materials, {} draws", data.name,
                               m_model->meshCount(), m_model->textureCount(), m_model->materials().size(),
                               m_draws.size());
            } else {
                m_demo = std::make_unique<DemoScene>(batch, scene());
                m_draws = m_demo->draws;
                m_bounds = m_demo->bounds;
            }
            batch.submit();
            ZODAR_LOG_INFO("Scene ready in {:.0f} ms",
                           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }

        LightingSettings lightingSettings;
        lightingSettings.quality = m_options.quality;
        lightingSettings.depthPrepass = m_options.prepass;
        scene().setLightingSettings(lightingSettings);
        if (m_options.environment != "none") {
            ProceduralSkyDesc sky;
            sky.sunDirection = {-0.5f, -1.0f, -0.6f};
            const ImageData source =
                m_options.environment == "sky" ? proceduralSky(1024, sky) : loadHdrImage(m_options.environment);
            m_environment = scene().ibl().bake(source, iblSettings(m_options.quality));
        }

        SceneView& view = m_view;
        view.clearColor = {0.06f, 0.07f, 0.09f, 1.0f};
        view.debugView = m_options.debugView;
        view.lighting.environment = m_environment.get();
        view.lighting.ambientIntensity = 0.25f;
        view.lighting.ambientColor = {0.75f, 0.85f, 1.0f};
        view.lighting.sun = {{-0.5f, -1.0f, -0.6f}, {1.0f, 0.96f, 0.9f}, 3.0f, true};
        view.camera.projection.nearPlane = std::max(0.01f, glm::length(m_bounds.extent()) * 0.001f);

        m_target = m_bounds.center();
        m_radius = std::max(0.5f * glm::length(m_bounds.extent()), 0.01f);
        m_yaw = glm::radians(m_options.yaw);
        m_pitch = glm::radians(m_options.pitch);
        m_distance = m_radius / std::sin(view.camera.projection.verticalFovRadians * 0.5f) / m_options.zoom;
        view.lighting.pointLights.push_back({m_target + glm::vec3(m_radius, m_radius * 0.8f, m_radius),
                                             {1.0f, 0.75f, 0.5f}, m_radius * m_radius * 4.0f, m_radius * 6.0f});
        // Optional swarm of small coloured lights orbiting the scene (clustered lighting demo).
        for (uint32_t i = 0; i < m_options.extraLights; ++i) {
            const float hue = float(i) * 0.618034f;
            const glm::vec3 color(0.5f + 0.5f * std::cos(6.2832f * hue),
                                  0.5f + 0.5f * std::cos(6.2832f * (hue + 0.33f)),
                                  0.5f + 0.5f * std::cos(6.2832f * (hue + 0.67f)));
            view.lighting.pointLights.push_back({m_target, color, m_radius * m_radius * 0.05f, m_radius * 0.35f});
        }
    }

    void onEvent(const SDL_Event& e) override {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F2) {
            m_view.debugView = LightingDebugView((uint32_t(m_view.debugView) + 1) % 7);
            ZODAR_LOG_INFO("Debug view: {}", toString(m_view.debugView));
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) m_dragging = true;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) m_dragging = false;
        if (e.type == SDL_EVENT_MOUSE_MOTION && m_dragging) {
            m_yaw -= e.motion.xrel * 0.005f;
            m_pitch = std::clamp(m_pitch - e.motion.yrel * 0.005f, -1.5f, 1.5f);
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL) m_distance *= e.wheel.y > 0 ? 0.9f : 1.1f;
    }

    void onUpdate(const FrameTime& time) override {
        if (m_options.referencePath) overlay().visible = false; // F1 must not add an overlay pass
        SceneView& view = m_view;
        m_yaw += glm::radians(m_options.spin) * static_cast<float>(time.dt);
        if (desc().camera) {
            const CameraOverride& c = *desc().camera;
            view.camera.position = c.position;
            view.camera.setYawPitch(glm::radians(c.yawDegrees), glm::radians(c.pitchDegrees));
        } else {
            const glm::vec3 offset{std::cos(m_pitch) * std::sin(m_yaw), -std::sin(m_pitch),
                                   std::cos(m_pitch) * std::cos(m_yaw)};
            view.camera.position = m_target + offset * m_distance;
            view.camera.lookAt(m_target);
        }
        view.time = static_cast<float>(time.time);
        for (uint32_t i = 0; i < m_options.extraLights; ++i) {
            const float phase = float(i) * 2.399963f + view.time * (0.2f + 0.05f * float(i % 7));
            const float ring = m_radius * (0.4f + 0.8f * float((i * 37) % 100) / 100.0f);
            view.lighting.pointLights[1 + i].position =
                m_target + glm::vec3(std::cos(phase) * ring, m_radius * (0.1f + 0.5f * float((i * 13) % 10) / 10.0f),
                                     std::sin(phase) * ring);
        }
        view.draws = m_draws;
    }

    void onRender(RenderContext& rc) override {
        if (m_options.referencePath) {
            rc.stats = scene().render(rc.info, rc.extent, m_view);
            scene().resolve(rc.info, rc.frame.imageView, rc.format, rc.extent);
        } else {
            scene().addToGraph(rc.graph, rc.info, rc.extent, m_view, rc.output, rc.format, &rc.stats);
        }
    }

    void onFrameEnd(const FrameTime& time) override {
        m_lastStats = lastRenderStats();
        ++m_statsFrames;
        const double elapsed = time.realTime - m_statsStart;
        if (elapsed >= 2.0) {
            ZODAR_LOG_INFO("{:.1f} FPS | draws {} (instances {}, culled {})", m_statsFrames / elapsed,
                           m_lastStats.drawCalls, m_lastStats.instances, m_lastStats.culled);
            m_statsStart = time.realTime;
            m_statsFrames = 0;
        }
    }

    void onShutdown() override {
        ZODAR_LOG_INFO("Rendered {} frames; last frame: {} draw calls, {} instances, {} culled",
                       renderer().frameNumber(), m_lastStats.drawCalls, m_lastStats.instances, m_lastStats.culled);
    }

private:
    Options m_options;
    std::unique_ptr<Model> m_model;
    std::unique_ptr<DemoScene> m_demo;
    std::vector<MeshDraw> m_draws;
    Bounds m_bounds;
    std::unique_ptr<Environment> m_environment;
    SceneView m_view;
    glm::vec3 m_target{0.0f};
    float m_radius = 1.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_distance = 1.0f;
    bool m_dragging = false;
    double m_statsStart = 0.0;
    uint64_t m_statsFrames = 0;
    RenderStats m_lastStats;
};

} // namespace

int main(int argc, char** argv) {
    return zodar::runMain(argc, argv, defaultDesc(), kUsage,
                          [](const zodar::ApplicationDesc& desc, std::span<const std::string> args) {
                              return std::make_unique<ViewerApp>(desc, parseOptions(args));
                          });
}
