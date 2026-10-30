#pragma once

#include "Renderer/Vulkan/SwapchainSupport.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace raven {
class Window;
}

namespace raven::gfx {

class VulkanContext;
class VulkanSwapchain;

struct RendererDesc {
    std::string applicationName = "RAVEN Application";
    bool enableValidation = true;
    PresentMode presentMode = PresentMode::VSync;
};

// Everything needed to record one frame. The swapchain image is already in
// COLOR_ATTACHMENT_OPTIMAL layout; the command buffer is in the recording state.
struct FrameContext {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint32_t imageIndex = 0;
    uint32_t frameSlot = 0;   // Index of the frame-in-flight resources.
    uint64_t frameNumber = 0; // Monotonic count of rendered frames.
};

// Milestone 1 frame orchestration: frames in flight, acquire/submit/present,
// swapchain recreation and optional swapchain readback. Higher-level rendering
// (render graph, materials, profiles) is built on top of this in Milestone 2+.
class Renderer {
public:
    static constexpr uint32_t kFramesInFlight = 2;

    Renderer(Window& window, const RendererDesc& desc);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Returns nullopt when no frame can be rendered now (minimized window or a
    // swapchain that had to be rebuilt). Callers simply skip drawing that iteration.
    std::optional<FrameContext> beginFrame();
    void endFrame(const FrameContext& frame);

    // Saves the next completed frame as a PPM file. The capture blocks until that
    // frame finishes on the GPU, so it is meant for tests and debugging only.
    void requestScreenshot(std::filesystem::path path);
    bool lastScreenshotSucceeded() const { return m_lastScreenshotOk; }

    void waitIdle() const;

    VulkanContext& context() { return *m_context; }
    const VulkanContext& context() const { return *m_context; }
    const VulkanSwapchain& swapchain() const { return *m_swapchain; }
    uint64_t frameNumber() const { return m_frameNumber; }

private:
    struct FrameResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    struct PendingCapture;

    void createFrameResources();
    void destroyFrameResources();
    void recreateSwapchain();
    void recordCapture(const FrameContext& frame);
    void finishCapture(const FrameContext& frame);

    Window& m_window;
    std::unique_ptr<VulkanContext> m_context;
    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::array<FrameResources, kFramesInFlight> m_frames{};
    uint32_t m_frameSlot = 0;
    uint64_t m_frameNumber = 0;
    bool m_swapchainDirty = false;

    std::optional<std::filesystem::path> m_screenshotRequest;
    std::unique_ptr<PendingCapture> m_capture;
    bool m_lastScreenshotOk = false;
};

} // namespace raven::gfx
