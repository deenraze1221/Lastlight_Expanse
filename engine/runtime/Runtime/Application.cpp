#include "Runtime/Application.h"

#include "Core/Error.h"
#include "Core/Log.h"

#include <spdlog/fmt/fmt.h>
#include "Debug/DebugOverlay.h"
#include "Platform/Platform.h"
#include "Platform/Window.h"
#include "Renderer/GpuProfiler.h"
#include "Renderer/RenderGraph/ResourceState.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <system_error>

namespace zodar {

using namespace zodar::gfx;

namespace {

void createParentDirectory(const std::filesystem::path& path) {
    if (!path.has_parent_path()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) ZODAR_LOG_WARN("Cannot create directory '{}': {}", path.parent_path().string(), ec.message());
}

// Paths in the run report are UTF-8 (path::string() is the ANSI code page on Windows).
std::string utf8(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

std::string versionString(uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

std::filesystem::path executableDirectory() {
    const char* base = SDL_GetBasePath();
    return std::filesystem::path(base ? base : "");
}

} // namespace

Application::Application(ApplicationDesc desc)
    : m_desc(std::move(desc)), m_timestep(m_desc.timestep) {
    if (m_desc.headless) {
        if (m_desc.maxFrames == 0) fail("Application: headless mode requires maxFrames > 0");
        // Must be set before SDL_Init (Platform). Overrides SDL_VIDEO_DRIVER from the environment.
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);
    }
    if (m_desc.screenshot && m_desc.maxFrames == 0) {
        ZODAR_LOG_WARN("A screenshot needs maxFrames > 0 (it is taken on the last frame); run() will return 3");
    }
    if (std::find(m_desc.captureFrames.begin(), m_desc.captureFrames.end(), 0u) != m_desc.captureFrames.end()) {
        fail("Application: captureFrames are 1-based (0 is invalid)");
    }
    std::sort(m_desc.captureFrames.begin(), m_desc.captureFrames.end());
    m_desc.captureFrames.erase(std::unique(m_desc.captureFrames.begin(), m_desc.captureFrames.end()),
                               m_desc.captureFrames.end());
    for (const uint64_t frame : m_desc.captureFrames) {
        if (m_desc.maxFrames > 0 && frame > m_desc.maxFrames) {
            ZODAR_LOG_WARN("Capture frame {} is after the last frame ({}); run() will return 3", frame,
                           m_desc.maxFrames);
        }
    }
    m_collectReport = m_desc.reportPath.has_value();

    m_platform = std::make_unique<Platform>();
    m_window = std::make_unique<Window>(WindowDesc{m_desc.name, m_desc.width, m_desc.height, m_desc.resizable});
    m_renderer = std::make_unique<Renderer>(
        *m_window, RendererDesc{m_desc.name, m_desc.validation, m_desc.presentMode,
                                Platform::userDataPath(m_desc.organization, m_desc.identifier) / "pipeline_cache.bin",
                                m_desc.synchronizationValidation, m_desc.dedicatedTransferQueue});
    m_upload = std::make_unique<UploadContext>(m_renderer->context());
    m_renderer->attachUploads(&m_upload->queue());

    m_shaderDirectory = executableDirectory() / "shaders";
    m_scene = std::make_unique<SceneRenderer>(m_renderer->context(), *m_upload,
                                              SceneRendererDesc{m_shaderDirectory, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                                VK_FORMAT_D32_SFLOAT, Renderer::kFramesInFlight});
    m_graphResources = std::make_unique<RenderGraphResources>(m_renderer->context(), m_scene->bindless());
    m_overlay = std::make_unique<debug::DebugOverlay>(m_renderer->context(), m_window.get(),
                                                      m_renderer->swapchain().format(),
                                                      m_renderer->swapchain().imageCount());
    m_overlay->visible = m_desc.overlay;
    m_jobs = std::make_unique<JobSystem>(m_desc.threads);
    ZODAR_LOG_INFO("Application '{}': {}x{}{}, fixed dt {:.4f} s{}, {} worker threads", m_desc.name, m_desc.width,
                   m_desc.height, m_desc.headless ? " headless" : "", m_desc.timestep.fixedDt,
                   m_desc.deterministic ? " (deterministic)" : "", m_jobs->workerCount());
}

Application::~Application() {
    // Derived members are gone; make sure nothing is in flight before engine teardown.
    if (m_renderer) m_renderer->waitIdle();
}

void Application::requestCapture(std::filesystem::path path) {
    createParentDirectory(path);
    m_pendingCaptures.push_back(std::move(path));
}

int Application::run() {
    int exitCode = 0;
    try {
        onStart();
        loop();
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
        exitCode = 1;
    }
    m_renderer->waitIdle();
    try {
        onShutdown();
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal during shutdown: {}", e.what());
        exitCode = 1;
    }
    if (exitCode == 0) exitCode = verifyOutputs();
    if (m_desc.reportPath && !writeReport(exitCode) && exitCode == 0) exitCode = 1;
    return exitCode;
}

int Application::verifyOutputs() const {
    if (m_renderer->context().validationEnabled() && m_renderer->context().validationErrorCount() > 0) {
        ZODAR_LOG_ERROR("{} Vulkan validation errors were reported", m_renderer->context().validationErrorCount());
        return 2;
    }
    const auto written = [&](uint64_t frame, const std::filesystem::path& path) {
        return std::any_of(m_captures.begin(), m_captures.end(), [&](const RunReport::Capture& c) {
            return c.ok && c.frame == frame && c.path == utf8(path);
        });
    };
    if (m_desc.screenshot && !written(m_renderer->frameNumber(), *m_desc.screenshot)) {
        ZODAR_LOG_ERROR("Screenshot '{}' was not written", m_desc.screenshot->string());
        return 3;
    }
    for (const uint64_t frame : m_desc.captureFrames) {
        if (!std::any_of(m_captures.begin(), m_captures.end(),
                         [&](const RunReport::Capture& c) { return c.ok && c.frame == frame; })) {
            ZODAR_LOG_ERROR("Capture of frame {} was not written", frame);
            return 3;
        }
    }
    // App captures (requestCapture): every attempt must have succeeded and none may be left.
    for (const RunReport::Capture& c : m_captures) {
        if (!c.ok) {
            ZODAR_LOG_ERROR("Capture '{}' (frame {}) was not written", c.path, c.frame);
            return 3;
        }
    }
    if (!m_pendingCaptures.empty()) {
        ZODAR_LOG_ERROR("Capture '{}' was requested but no frame was rendered after it",
                        m_pendingCaptures.front().string());
        return 3;
    }
    return 0;
}

bool Application::writeReport(int exitCode) {
    RunReport& r = m_report;
    const VulkanContext& ctx = m_renderer->context();
    const DeviceInfo& device = ctx.deviceInfo();
    r.application = m_desc.name;
    r.exitCode = exitCode;
    r.frames = m_renderer->frameNumber();
    r.fixedSteps = m_timestep.totalSteps();
    r.wallSeconds = m_loopSeconds;
    r.simulatedSeconds = m_timestep.simulationTime();
    r.fixedDt = m_timestep.fixedDt();
    r.headless = m_desc.headless;
    r.deterministic = m_desc.deterministic;
    r.width = m_desc.width;
    r.height = m_desc.height;
    r.seed = m_desc.seed;
    r.deviceName = device.name;
    r.driver = device.driverName + " " + device.driverInfo;
    r.apiVersion = versionString(device.effectiveApiVersion);
    r.softwareRasterizer = device.isSoftwareRasterizer;
    r.validation = ctx.validationEnabled();
    r.validationErrors = ctx.validationErrorCount();
    r.validationWarnings = ctx.validationWarningCount();
    const log::Counts counts = log::counts();
    r.logWarnings = counts.warnings;
    r.logErrors = counts.errors;
    r.render = m_renderStats;
    r.memory = queryMemoryStats(ctx);
    r.peakDeviceLocalUsage = std::max(r.peakDeviceLocalUsage, r.memory.deviceLocalUsage);
    r.screenshots = m_captures;

    createParentDirectory(*m_desc.reportPath);
    if (writeRunReport(*m_desc.reportPath, r)) {
        ZODAR_LOG_INFO("Run report written: {}", m_desc.reportPath->string());
        return true;
    }
    ZODAR_LOG_ERROR("Failed to write run report '{}'", m_desc.reportPath->string());
    return false;
}

void Application::loop() {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start;
    FrameTime time;
    if (m_desc.screenshot) createParentDirectory(*m_desc.screenshot);
    if (!m_desc.captureFrames.empty()) createParentDirectory(m_desc.captureDir / "frame");

    while (!m_quit && !m_window->shouldClose()) {
        m_cpuProfiler.beginFrame();
        m_window->pollEvents([&](const SDL_Event& e) {
            if (m_overlay->processEvent(e)) return;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) requestQuit();
            onEvent(e);
        });

        const auto now = Clock::now();
        time.realDt = std::chrono::duration<double>(now - last).count();
        time.realTime = std::chrono::duration<double>(now - start).count();
        last = now;
        // One clock for both: the clamped frame dt also feeds the fixed-step accumulator.
        time.dt = m_desc.deterministic ? m_timestep.fixedDt()
                                       : std::min(time.realDt, static_cast<double>(m_desc.maxFrameDt));
        time.fixedSteps = m_timestep.advance(time.dt);
        for (uint32_t i = 0; i < time.fixedSteps; ++i) {
            ZODAR_CPU_SCOPE(m_cpuProfiler, "Fixed update");
            onFixedUpdate(m_timestep.fixedDt());
        }
        time.alpha = m_timestep.alpha();
        {
            ZODAR_CPU_SCOPE(m_cpuProfiler, "Update");
            onUpdate(time);
        }

        const bool lastFrame = m_desc.maxFrames > 0 && m_renderer->frameNumber() + 1 >= m_desc.maxFrames;
        renderFrame(time, lastFrame);
        time.time += time.dt;
        ++time.frame;
        if (lastFrame && m_renderer->frameNumber() >= m_desc.maxFrames) break;
    }
    m_loopSeconds = std::chrono::duration<double>(Clock::now() - start).count();
    ZODAR_LOG_INFO("Rendered {} frames, {} fixed steps ({:.2f} s simulated)", m_renderer->frameNumber(),
                   m_timestep.totalSteps(), m_timestep.simulationTime());
}

void Application::renderFrame(const FrameTime& time, bool lastFrame) {
    // Captures: one readback per frame, written to every requested path (the last frame can be
    // both the --screenshot and a --capture-frames entry).
    const uint64_t frameNumber = m_renderer->frameNumber() + 1; // 1-based number of this frame
    const bool captureThis = std::binary_search(m_desc.captureFrames.begin(), m_desc.captureFrames.end(), frameNumber);
    const std::filesystem::path capturePath =
        m_desc.captureDir / fmt::format("frame_{:06}.png", frameNumber);
    std::vector<std::filesystem::path> request;
    if (lastFrame && m_desc.screenshot) request.push_back(*m_desc.screenshot);
    if (captureThis) request.push_back(capturePath);
    request.insert(request.end(), m_pendingCaptures.begin(), m_pendingCaptures.end());
    if (!request.empty()) m_renderer->requestScreenshot(request);

    const uint32_t acquireScope = m_cpuProfiler.begin("Wait + acquire");
    auto frame = m_renderer->beginFrame();
    m_cpuProfiler.end(acquireScope);
    if (!frame) {
        // Minimized or swapchain out of date: skip rendering this iteration.
        m_cpuProfiler.endFrame();
        SDL_Delay(5);
        return;
    }
    m_pendingCaptures.clear(); // part of this frame's request
    const uint32_t buildScope = m_cpuProfiler.begin("Build graph");
    const FrameInfo info = frame->info();
    RenderGraph graph(*m_graphResources);
    RGImportedImage swapchainImage;
    swapchainImage.image = frame->image;
    swapchainImage.view = frame->imageView;
    swapchainImage.format = frame->format;
    swapchainImage.extent = frame->extent;
    swapchainImage.initial = usageState(ResourceUsage::ColorAttachment);
    swapchainImage.final = usageState(ResourceUsage::ColorAttachment);
    const RGImage output = graph.importImage("Swapchain", swapchainImage);

    RenderContext context{graph, info, frame->extent, output, frame->format, m_renderStats, *frame};
    onRender(context);

    m_overlay->newFrame(frame->extent, static_cast<float>(time.dt));
    debug::ProfilerSnapshot snapshot;
    snapshot.cpu = &m_cpuProfiler;
    snapshot.gpu = &m_renderer->profiler();
    snapshot.cpuFrameMs = &m_cpuFrameMs;
    snapshot.gpuFrameMs = &m_gpuFrameMs;
    snapshot.render = m_renderStats;
    // Memory query only when someone reads it (visible overlay, run report).
    if (m_overlay->visible || m_collectReport) snapshot.memory = queryMemoryStats(m_renderer->context());
    snapshot.resolution = frame->extent;
    snapshot.frameNumber = frame->frameNumber;
    m_overlay->drawProfiler(snapshot);
    m_overlay->addToGraph(graph, output);
    m_cpuProfiler.end(buildScope);

    {
        ZODAR_CPU_SCOPE(m_cpuProfiler, "Record");
        graph.execute(info);
    }
    {
        ZODAR_CPU_SCOPE(m_cpuProfiler, "Submit + present");
        m_renderer->endFrame(*frame); // a requested screenshot is written before this returns
    }
    m_cpuProfiler.endFrame();
    m_cpuFrameMs.push(static_cast<float>(m_cpuProfiler.lastFrameMs()));
    const GpuProfiler& gpu = m_renderer->profiler();
    if (gpu.enabled()) {
        m_gpuFrameMs.push(static_cast<float>(gpu.lastFrameMs()));
    }

    if (!request.empty()) {
        const bool ok = m_renderer->lastScreenshotSucceeded();
        for (const std::filesystem::path& path : request) {
            m_captures.push_back({utf8(path), frameNumber, ok});
        }
    }

    if (m_collectReport) {
        if (time.frame > 0) m_report.frameMs.push_back(time.realDt * 1000.0); // no interval before frame 1
        m_report.cpuFrameMs.push_back(m_cpuProfiler.lastFrameMs());
        for (const CpuProfiler::Scope& scope : m_cpuProfiler.lastFrame()) {
            m_report.cpuScopes.add(scope.name, scope.ms);
        }
        if (gpu.enabled() && !gpu.lastResults().empty()) {
            m_report.gpuFrameMs.push_back(gpu.lastFrameMs());
            for (const GpuProfiler::Scope& scope : gpu.lastResults()) {
                m_report.gpuPasses.add(scope.name, scope.ms);
            }
        }
        m_report.peakDeviceLocalUsage = std::max(m_report.peakDeviceLocalUsage, snapshot.memory.deviceLocalUsage);
    }
    onFrameEnd(time);
}

} // namespace zodar
