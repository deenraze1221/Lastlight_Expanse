#include "Renderer/Vulkan/VulkanSwapchain.h"

#include "Core/Log.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

namespace raven::gfx {
namespace {

const char* presentModeName(VkPresentModeKHR mode) {
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
    default: return "OTHER";
    }
}

} // namespace

VulkanSwapchain::VulkanSwapchain(const VulkanContext& context, VkExtent2D framebuffer, PresentMode presentMode)
    : m_context(context), m_preference(presentMode) {
    try {
        create(framebuffer, VK_NULL_HANDLE);
    } catch (...) {
        destroyImageResources();
        if (m_swapchain) {
            vkDestroySwapchainKHR(m_context.device(), m_swapchain, nullptr);
        }
        throw;
    }
}

VulkanSwapchain::~VulkanSwapchain() {
    destroyImageResources();
    if (m_swapchain) {
        vkDestroySwapchainKHR(m_context.device(), m_swapchain, nullptr);
    }
}

void VulkanSwapchain::recreate(VkExtent2D framebuffer) {
    destroyImageResources();
    const VkSwapchainKHR old = m_swapchain;
    m_swapchain = VK_NULL_HANDLE;
    try {
        create(framebuffer, old);
    } catch (...) {
        vkDestroySwapchainKHR(m_context.device(), old, nullptr);
        throw;
    }
    vkDestroySwapchainKHR(m_context.device(), old, nullptr);
}

void VulkanSwapchain::create(VkExtent2D framebuffer, VkSwapchainKHR oldSwapchain) {
    const VkPhysicalDevice gpu = m_context.physicalDevice();
    const VkSurfaceKHR surface = m_context.surface();
    const VkDevice device = m_context.device();

    VkSurfaceCapabilitiesKHR caps;
    RAVEN_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));

    uint32_t formatCount = 0;
    RAVEN_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    RAVEN_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, formats.data()));

    uint32_t modeCount = 0;
    RAVEN_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, nullptr));
    std::vector<VkPresentModeKHR> modes(modeCount);
    RAVEN_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, modes.data()));

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    m_format = surfaceFormat.format;
    m_presentMode = choosePresentMode(modes, m_preference);
    m_extent = chooseExtent(caps, framebuffer);
    m_supportsReadback = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = chooseImageCount(caps);
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = m_extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (m_supportsReadback) {
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    const uint32_t families[] = {m_context.graphicsQueueFamily(), m_context.presentQueueFamily()};
    if (families[0] != families[1]) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    info.presentMode = m_presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = oldSwapchain;

    RAVEN_VK_CHECK(vkCreateSwapchainKHR(device, &info, nullptr, &m_swapchain));

    uint32_t imageCount = 0;
    RAVEN_VK_CHECK(vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, nullptr));
    m_images.resize(imageCount);
    RAVEN_VK_CHECK(vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, m_images.data()));

    for (VkImage image : m_images) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = m_format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView handle = VK_NULL_HANDLE;
        RAVEN_VK_CHECK(vkCreateImageView(device, &view, nullptr, &handle));
        m_views.push_back(handle);

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        RAVEN_VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
        m_renderFinished.push_back(semaphore);
    }

    RAVEN_LOG_INFO("Swapchain {}x{}, {} images, present mode {}", m_extent.width, m_extent.height, imageCount,
                   presentModeName(m_presentMode));
}

void VulkanSwapchain::destroyImageResources() {
    const VkDevice device = m_context.device();
    for (VkSemaphore semaphore : m_renderFinished) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    for (VkImageView view : m_views) {
        vkDestroyImageView(device, view, nullptr);
    }
    m_renderFinished.clear();
    m_views.clear();
    m_images.clear();
}

} // namespace raven::gfx
