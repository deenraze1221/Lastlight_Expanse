#include "Debug/DebugOverlay.h"

#include "Core/Error.h"
#include "Platform/Window.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace zodar::debug {
namespace {

void checkResult(VkResult result) {
    if (result != VK_SUCCESS) {
        fail(std::string("Dear ImGui Vulkan backend error: ") + std::to_string(static_cast<int>(result)));
    }
}

// ImGui colours are authored in sRGB; the overlay may render into an sRGB target that
// encodes again, so convert the style to linear for such targets.
void linearizeStyle() {
    for (ImVec4& c : ImGui::GetStyle().Colors) {
        c.x = std::pow(c.x, 2.2f);
        c.y = std::pow(c.y, 2.2f);
        c.z = std::pow(c.z, 2.2f);
    }
}

bool isSrgb(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}

std::string bytes(uint64_t b) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MiB", static_cast<double>(b) / (1024.0 * 1024.0));
    return text;
}

} // namespace

DebugOverlay::DebugOverlay(gfx::VulkanContext& context, Window* window, VkFormat targetFormat, uint32_t imageCount)
    : m_context(context), m_window(window), m_format(targetFormat) {
    IMGUI_CHECKVERSION();
    ZODAR_ENSURE(ImGui::GetCurrentContext() == nullptr, "only one DebugOverlay may exist at a time");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // No imgui.ini next to the executable.
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.0f;
    if (isSrgb(targetFormat)) {
        linearizeStyle();
    }
    if (window) {
        ImGui_ImplSDL3_InitForVulkan(window->native());
    }

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = context.instance();
    info.PhysicalDevice = context.physicalDevice();
    info.Device = context.device();
    info.QueueFamily = context.graphicsQueueFamily();
    info.Queue = context.graphicsQueue();
    info.DescriptorPoolSize = 64;
    info.MinImageCount = std::max(2u, imageCount);
    info.ImageCount = std::max(2u, imageCount);
    info.PipelineCache = context.pipelineCache();
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_format;
    info.CheckVkResultFn = checkResult;
    if (!ImGui_ImplVulkan_Init(&info)) {
        if (window) ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        fail("Dear ImGui Vulkan backend initialization failed");
    }
}

DebugOverlay::~DebugOverlay() {
    vkDeviceWaitIdle(m_context.device());
    if (m_frameStarted) {
        ImGui::EndFrame();
    }
    ImGui_ImplVulkan_Shutdown();
    if (m_window) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext();
}

bool DebugOverlay::processEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat) {
        visible = !visible;
    }
    if (!m_window || !visible) {
        return false;
    }
    ImGui_ImplSDL3_ProcessEvent(&event);
    const ImGuiIO& io = ImGui::GetIO();
    const bool mouse = event.type >= SDL_EVENT_MOUSE_MOTION && event.type <= SDL_EVENT_MOUSE_WHEEL;
    const bool keyboard = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
    return (mouse && io.WantCaptureMouse) || (keyboard && io.WantCaptureKeyboard);
}

void DebugOverlay::newFrame(VkExtent2D displaySize, float deltaSeconds) {
    ImGui_ImplVulkan_NewFrame();
    if (m_window) {
        ImGui_ImplSDL3_NewFrame();
    } else {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(displaySize.width), static_cast<float>(displaySize.height));
        io.DeltaTime = deltaSeconds > 0.0f ? deltaSeconds : 1.0f / 60.0f;
    }
    ImGui::NewFrame();
    m_frameStarted = true;
}

void DebugOverlay::drawProfiler(const ProfilerSnapshot& s) {
    if (!visible) {
        return;
    }
    const auto& info = m_context.deviceInfo();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (!ImGui::Begin("ZODAR Profiler (F1)")) {
        ImGui::End();
        return;
    }
    ImGui::Text("%s", info.name.c_str());
    if (info.isSoftwareRasterizer) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.1f, 1.0f), "Software rasterizer: timings are not GPU performance");
    }
    ImGui::Text("%ux%u  frame %llu  validation %u err / %u warn", s.resolution.width, s.resolution.height,
                static_cast<unsigned long long>(s.frameNumber), m_context.validationErrorCount(),
                m_context.validationWarningCount());

    const auto plot = [](const char* label, const History* h) {
        if (!h || h->count() == 0) return;
        char overlay[96];
        std::snprintf(overlay, sizeof(overlay), "avg %.2f  p99 %.2f  max %.2f ms", h->average(), h->percentile(0.99f),
                      h->max());
        ImGui::PlotLines(label, h->values().data(), static_cast<int>(h->values().size()), static_cast<int>(h->offset()),
                         overlay, 0.0f, std::max(h->max() * 1.2f, 1.0f), ImVec2(0, 48));
    };
    if (ImGui::CollapsingHeader("Frame time", ImGuiTreeNodeFlags_DefaultOpen)) {
        plot("CPU", s.cpuFrameMs);
        plot("GPU", s.gpuFrameMs);
    }
    if (ImGui::CollapsingHeader("GPU passes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (s.gpu && s.gpu->enabled()) {
            for (const auto& scope : s.gpu->lastResults()) {
                ImGui::Text("%*s%-24s %7.3f ms", scope.depth * 2, "", scope.name.c_str(), scope.ms);
            }
        } else {
            ImGui::TextDisabled("GPU timestamps unavailable (fallback: CPU timers)");
        }
    }
    if (ImGui::CollapsingHeader("CPU scopes", ImGuiTreeNodeFlags_DefaultOpen) && s.cpu) {
        for (const auto& scope : s.cpu->lastFrame()) {
            ImGui::Text("%*s%-24s %7.3f ms", scope.depth * 2, "", scope.name.c_str(), scope.ms);
        }
    }
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Draw calls %u   instances %u   culled %u / %u", s.render.drawCalls, s.render.instances,
                    s.render.culled, s.render.submitted);
        ImGui::Text("Triangles %llu", static_cast<unsigned long long>(s.render.triangles));
    }
    if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Device local %s / %s%s", bytes(s.memory.deviceLocalUsage).c_str(),
                    bytes(s.memory.deviceLocalBudget).c_str(), s.memory.budgetFromDriver ? "" : " (estimate)");
        ImGui::Text("Allocations %u (%s)", s.memory.allocationCount, bytes(s.memory.allocationBytes).c_str());
    }
    if (ImGui::CollapsingHeader("Device features")) {
        for (const auto& e : m_context.features().entries()) {
            if (e.enabled) {
                ImGui::Text("%-38s yes", e.name);
            } else {
                ImGui::TextDisabled("%-38s no -> %s", e.name, e.fallback);
            }
        }
    }
    ImGui::End();
}

void DebugOverlay::addToGraph(gfx::RenderGraph& graph, const gfx::RGImage& target) {
    ZODAR_ENSURE(m_frameStarted, "DebugOverlay::addToGraph without newFrame()");
    ImGui::Render();
    m_frameStarted = false;
    ImDrawData* data = ImGui::GetDrawData();
    if (!visible || !data || data->TotalVtxCount == 0) {
        return;
    }
    graph.addPass(
        "DebugOverlay", gfx::RenderGraph::PassType::Raster,
        [&](gfx::PassBuilder& b) { b.colorAttachment(target, gfx::LoadOp::Load); },
        [](gfx::PassContext& ctx) { ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx.cmd()); });
}

} // namespace zodar::debug
