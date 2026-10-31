#pragma once

// zodar::Application - the standard engine application runtime.
//
// Owns Platform, Window (visible or headless offscreen), Renderer, UploadContext, SceneRenderer,
// RenderGraphResources, DebugOverlay, CpuProfiler and JobSystem, and runs the standard frame
// loop. Apps derive from it and override hooks:
//
//   class MyApp : public zodar::Application {
//   public:
//       MyApp() : Application(makeDesc()) {}
//   protected:
//       void onStart() override { /* create GPU resources, load world */ }
//       void onFixedUpdate(double dt) override { /* deterministic simulation */ }
//       void onUpdate(const zodar::FrameTime& t) override { /* camera, streaming */ }
//       void onRender(zodar::RenderContext& rc) override {
//           scene().addToGraph(rc.graph, rc.info, rc.extent, m_view, rc.output, rc.format, &rc.stats);
//       }
//   };
//   int main() { return MyApp().run(); }
//
// Frame order: events -> fixed steps (onFixedUpdate x N) -> onUpdate -> [beginFrame ->
// onRender -> debug overlay -> graph execute -> endFrame]. The subsystems exist from the
// constructor on; hooks run only inside run(). Members of the derived class are destroyed
// before the engine subsystems; run() waits for the GPU before returning. A derived class that
// creates GPU objects without calling run() must call renderer().waitIdle() in its destructor.
//
// Exit codes from run(): 0 ok, 1 fatal error (exception, or the run report could not be
// written), 2 Vulkan validation errors, 3 screenshot or capture requested but not written.
// Captures (--capture-frames) and the run report (--report) are handled by the runtime.

#include "Core/JobSystem.h"
#include "Core/Profiler.h"
#include "Renderer/FrameInfo.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/SceneRenderer.h"
#include "Runtime/FixedTimestep.h"
#include "Runtime/RunReport.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

union SDL_Event;

namespace zodar {

class Platform;
class Window;

namespace gfx {
class UploadContext;
}
namespace debug {
class DebugOverlay;
}

// Start camera requested on the command line (--camera). The runtime owns no camera: apps
// that have one apply it in onStart.
struct CameraOverride {
    glm::vec3 position{0.0f};
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
};

// Settings of an Application. The standard command line (Runtime/CommandLine.h) sets most
// fields; `--help` lists the options.
struct ApplicationDesc {
    std::string name = "ZODAR Application";  // window title, Vulkan application name
    // Per-user data directory <organization>/<identifier> (pipeline cache). No spaces needed.
    std::string organization = "ZODAR";
    std::string identifier = "ZodarApplication";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
    // No visible window: SDL "offscreen" video driver (Vulkan via VK_EXT_headless_surface).
    // Requires maxFrames > 0 so the app terminates.
    bool headless = false;
    bool validation = true;
    gfx::PresentMode presentMode = gfx::PresentMode::VSync;
    bool overlay = false;                    // profiler overlay visible at start (F1 toggles)
    uint64_t maxFrames = 0;                  // 0 = until the window closes
    std::optional<std::filesystem::path> screenshot; // written on the last frame (needs maxFrames, else exit 3)
    // Simulation clock. deterministic: every frame advances exactly one fixed step and frame
    // time is fixedDt, independent of the wall clock (same input -> same frames).
    FixedTimestepDesc timestep{};
    bool deterministic = false;
    float maxFrameDt = 0.1f;                 // clamp for FrameTime::dt and the fixed-step input
    uint32_t threads = JobSystem::kAutomatic; // JobSystem workers (0 = inline)
    // Extra captures: 1-based frame numbers, written as <captureDir>/frame_NNNNNN.png.
    std::vector<uint64_t> captureFrames;
    std::filesystem::path captureDir = "captures";
    // Run report JSON written when run() ends (frame times, scopes, stats, validation, captures).
    std::optional<std::filesystem::path> reportPath;
    // Log file opened by runMain() before the app is constructed (empty: console only).
    std::filesystem::path logFile;
    // App-defined meaning; reported in the run report. Empty: the app's default seed.
    std::optional<uint64_t> seed;
    std::optional<CameraOverride> camera;
};

// Per-frame timing passed to onUpdate.
struct FrameTime {
    double dt = 0.0;         // frame delta in seconds (fixedDt when deterministic; clamped)
    double time = 0.0;       // accumulated dt of previous frames (0 on frame 0; deterministic clock)
    double realDt = 0.0;     // unclamped wall-clock delta (statistics only)
    double realTime = 0.0;   // wall-clock seconds since the loop started
    double alpha = 0.0;      // fixed-step interpolation weight in [0, 1)
    uint32_t fixedSteps = 0; // fixed steps run this frame
    uint64_t frame = 0;      // loop iteration index (0-based)
};

// Everything onRender needs to record into this frame's render graph.
struct RenderContext {
    gfx::RenderGraph& graph;
    const gfx::FrameInfo& info;
    VkExtent2D extent;
    gfx::RGImage output; // swapchain image (colour attachment)
    VkFormat format;
    gfx::RenderStats& stats; // fill for the profiler overlay (SceneRenderer::addToGraph)
};

class Application {
public:
    explicit Application(ApplicationDesc desc);
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs onStart, the frame loop and onShutdown; returns the exit code (see top).
    int run();
    // Ends the loop after the current frame.
    void requestQuit() { m_quit = true; }

    const ApplicationDesc& desc() const { return m_desc; }
    Platform& platform() { return *m_platform; }
    Window& window() { return *m_window; }
    gfx::Renderer& renderer() { return *m_renderer; }
    gfx::UploadContext& upload() { return *m_upload; }
    gfx::SceneRenderer& scene() { return *m_scene; }
    gfx::RenderGraphResources& graphResources() { return *m_graphResources; }
    debug::DebugOverlay& overlay() { return *m_overlay; }
    JobSystem& jobs() { return *m_jobs; }
    CpuProfiler& cpuProfiler() { return m_cpuProfiler; }
    const FixedTimestep& timestep() const { return m_timestep; }
    const gfx::RenderStats& lastRenderStats() const { return m_renderStats; }
    // Directory next to the executable holding compiled shaders (engine + app).
    const std::filesystem::path& shaderDirectory() const { return m_shaderDirectory; }

protected:
    virtual void onStart() {}
    // Every OS event not consumed by the overlay (Escape already requests quit).
    virtual void onEvent(const SDL_Event& event) { (void)event; }
    virtual void onFixedUpdate(double dt) { (void)dt; }
    virtual void onUpdate(const FrameTime& time) { (void)time; }
    virtual void onRender(RenderContext& context) { (void)context; }
    // After the GPU is idle; GPU resources may be released here or in the destructor.
    virtual void onShutdown() {}

private:
    void loop();
    void renderFrame(const FrameTime& time, bool lastFrame);
    int verifyOutputs() const;
    bool writeReport(int exitCode);

    ApplicationDesc m_desc;
    std::filesystem::path m_shaderDirectory;
    std::unique_ptr<Platform> m_platform;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<gfx::Renderer> m_renderer;
    std::unique_ptr<gfx::UploadContext> m_upload;
    std::unique_ptr<gfx::SceneRenderer> m_scene;
    std::unique_ptr<gfx::RenderGraphResources> m_graphResources;
    std::unique_ptr<debug::DebugOverlay> m_overlay;
    std::unique_ptr<JobSystem> m_jobs;
    CpuProfiler m_cpuProfiler;
    History m_cpuFrameMs;
    History m_gpuFrameMs;
    FixedTimestep m_timestep;
    gfx::RenderStats m_renderStats;
    bool m_quit = false;
    // Captures attempted (--screenshot, --capture-frames) and run-report samples.
    std::vector<RunReport::Capture> m_captures;
    bool m_collectReport = false;
    RunReport m_report;
    double m_loopSeconds = 0.0;
};

} // namespace zodar
