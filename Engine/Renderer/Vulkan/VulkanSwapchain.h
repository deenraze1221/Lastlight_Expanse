#pragma once

#include "Renderer/Vulkan/SwapchainSupport.h"

#include <volk.h>

#include <vector>

namespace raven::gfx {

class VulkanContext;

// Swapchain images, their views and the per-image "render finished" semaphores.
// Present-wait semaphores are per image (not per frame in flight) because the
// presentation engine may still hold one when the next frame reuses its slot.
class VulkanSwapchain {
public:
    VulkanSwapchain(const VulkanContext& context, VkExtent2D framebuffer, PresentMode presentMode);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // Rebuilds for a new framebuffer size. The caller must ensure the GPU is idle.
    void recreate(VkExtent2D framebuffer);

    VkSwapchainKHR handle() const { return m_swapchain; }
    VkFormat format() const { return m_format; }
    VkExtent2D extent() const { return m_extent; }
    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }
    VkImage image(uint32_t index) const { return m_images[index]; }
    VkImageView imageView(uint32_t index) const { return m_views[index]; }
    VkSemaphore renderFinished(uint32_t index) const { return m_renderFinished[index]; }
    VkPresentModeKHR presentMode() const { return m_presentMode; }
    bool supportsReadback() const { return m_supportsReadback; }

private:
    void create(VkExtent2D framebuffer, VkSwapchainKHR oldSwapchain);
    void destroyImageResources();

    const VulkanContext& m_context;
    PresentMode m_preference;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool m_supportsReadback = false;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkSemaphore> m_renderFinished;
};

} // namespace raven::gfx
