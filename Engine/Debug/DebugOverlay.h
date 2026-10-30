#pragma once

#include "Core/Profiler.h"
#include "Renderer/GpuProfiler.h"
#include "Renderer/SceneRenderer.h"

#include <volk.h>

#include <string>
#include <vector>

union SDL_Event;

namespace zodar {
class Window;
}

namespace zodar::gfx {
class RenderGraph;
class VulkanContext;
struct RGImage;
}

namespace zodar::debug {

// Everything the profiler window shows for one frame. Filled by the application.
struct ProfilerSnapshot {
    const CpuProfiler* cpu = nullptr;
    const gfx::GpuProfiler* gpu = nullptr;
    const History* cpuFrameMs = nullptr;
    const History* gpuFrameMs = nullptr;
    gfx::RenderStats render;
    gfx::GpuMemoryStats memory;
    VkExtent2D resolution{};
    uint64_t frameNumber = 0;
};

// Dear ImGui overlay for debugging and profiling (engine tool, not game UI).
// One instance at a time (ImGui keeps a global context). Without a window it runs
// headless: no input, display size from newFrame().
class DebugOverlay {
public:
    DebugOverlay(gfx::VulkanContext& context, Window* window, VkFormat targetFormat, uint32_t imageCount);
    ~DebugOverlay();

    DebugOverlay(const DebugOverlay&) = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;

    // Returns true if ImGui consumed the event (callers should then ignore it).
    bool processEvent(const SDL_Event& event);
    void newFrame(VkExtent2D displaySize, float deltaSeconds);
    void drawProfiler(const ProfilerSnapshot& snapshot);
    // Adds a raster pass that draws the UI on top of `target` (loads existing contents).
    void addToGraph(gfx::RenderGraph& graph, const gfx::RGImage& target);

    bool visible = true;

private:
    gfx::VulkanContext& m_context;
    Window* m_window;
    VkFormat m_format;
    bool m_frameStarted = false;
};

} // namespace zodar::debug
