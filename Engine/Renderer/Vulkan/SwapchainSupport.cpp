#include "Renderer/Vulkan/SwapchainSupport.h"

#include <algorithm>

namespace zodar::gfx {
namespace {

bool contains(std::span<const VkPresentModeKHR> modes, VkPresentModeKHR mode) {
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

} // namespace

VkSurfaceFormatKHR chooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> available) {
    // sRGB swapchain: shaders output linear colour, hardware applies the transfer function.
    constexpr VkFormat preferred[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
    for (VkFormat format : preferred) {
        for (const auto& candidate : available) {
            if (candidate.format == format && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return candidate;
            }
        }
    }
    return available.empty() ? VkSurfaceFormatKHR{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                             : available.front();
}

VkPresentModeKHR choosePresentMode(std::span<const VkPresentModeKHR> available, PresentMode preference) {
    if (preference == PresentMode::Immediate && contains(available, VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    if (preference != PresentMode::VSync && contains(available, VK_PRESENT_MODE_MAILBOX_KHR)) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR; // Guaranteed by the specification.
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D framebuffer) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    return {std::clamp(framebuffer.width, caps.minImageExtent.width, caps.maxImageExtent.width),
            std::clamp(framebuffer.height, caps.minImageExtent.height, caps.maxImageExtent.height)};
}

uint32_t chooseImageCount(const VkSurfaceCapabilitiesKHR& caps) {
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && count > caps.maxImageCount) {
        count = caps.maxImageCount;
    }
    return count;
}

const char* toString(PresentMode mode) {
    switch (mode) {
    case PresentMode::VSync: return "vsync";
    case PresentMode::LowLatency: return "low-latency";
    case PresentMode::Immediate: return "immediate";
    }
    return "unknown";
}

} // namespace zodar::gfx
