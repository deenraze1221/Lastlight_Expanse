#include "Runtime/Application.h"

#include "Core/Error.h"
#include "Core/Log.h"
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

namespace zodar {

using namespace zodar::gfx;

namespace {

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

    m_platform = std::make_unique<Platform>();
    m_window = std::make_unique<Window>(WindowDesc{m_desc.name, m_desc.width, m_desc.height, m_desc.resizable});
    m_renderer = std::make_unique<Renderer>(
        *m_window, RendererDesc{m_desc.name, m_desc.validation, m_desc.presentMode,
                                Platform::userDataPath(m_desc.organization, m_desc.identifier) / "pipeline_cache.bin"});
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

int Application::run() {
    int exitCode = 0;
    try {
        onStart();
        exitCode = loop();
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
    if (exitCode != 0) return exitCode;

    if (m_renderer->context().validationEnabled() && m_renderer->context().validationErrorCount() > 0) {
        ZODAR_LOG_ERROR("{} Vulkan validation errors were reported", m_renderer->context().validationErrorCount());
        return 2;
    }
    if (m_desc.screenshot && !m_renderer->lastScreenshotSucceeded()) {
        ZODAR_LOG_ERROR("Screenshot '{}' was not written", m_desc.screenshot->string());
        return 3;
    }
    return 0;
}

int Application::loop() {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start;
    FrameTime time;

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
    ZODAR_LOG_INFO("Rendered {} frames, {} fixed steps ({:.2f} s simulated)", m_renderer->frameNumber(),
                   m_timestep.totalSteps(), m_timestep.simulationTime());
    return 0;
}

void Application::renderFrame(const FrameTime& time, bool lastFrame) {
    if (lastFrame && m_desc.screenshot) {
        m_renderer->requestScreenshot(*m_desc.screenshot);
    }
    auto frame = m_renderer->beginFrame();
    if (!frame) {
        // Minimized or swapchain out of date: skip rendering this iteration.
        m_cpuProfiler.endFrame();
        SDL_Delay(5);
        return;
    }
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

    RenderContext context{graph, info, frame->extent, output, frame->format, m_renderStats};
    onRender(context);

    m_overlay->newFrame(frame->extent, static_cast<float>(time.dt));
    debug::ProfilerSnapshot snapshot;
    snapshot.cpu = &m_cpuProfiler;
    snapshot.gpu = &m_renderer->profiler();
    snapshot.cpuFrameMs = &m_cpuFrameMs;
    snapshot.gpuFrameMs = &m_gpuFrameMs;
    snapshot.render = m_renderStats;
    snapshot.memory = queryMemoryStats(m_renderer->context());
    snapshot.resolution = frame->extent;
    snapshot.frameNumber = frame->frameNumber;
    m_overlay->drawProfiler(snapshot);
    m_overlay->addToGraph(graph, output);

    graph.execute(info);
    m_renderer->endFrame(*frame);
    m_cpuProfiler.endFrame();
    m_cpuFrameMs.push(static_cast<float>(m_cpuProfiler.lastFrameMs()));
    if (m_renderer->profiler().enabled()) {
        m_gpuFrameMs.push(static_cast<float>(m_renderer->profiler().lastFrameMs()));
    }
}

} // namespace zodar
