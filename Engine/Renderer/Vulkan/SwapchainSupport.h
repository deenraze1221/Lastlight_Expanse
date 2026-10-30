#pragma once

#include <volk.h>

#include <cstdint>
#include <span>

namespace raven::gfx {

enum class PresentMode {
    VSync,       // FIFO: always available, no tearing.
    LowLatency,  // MAILBOX if available, otherwise FIFO.
    Immediate,   // IMMEDIATE if available, otherwise MAILBOX, otherwise FIFO. May tear.
};

// Pure policy functions: no Vulkan calls, unit tested.
VkSurfaceFormatKHR chooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> available);
VkPresentModeKHR choosePresentMode(std::span<const VkPresentModeKHR> available, PresentMode preference);
VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D framebuffer);
uint32_t chooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities);

const char* toString(PresentMode mode);

// True for 8-bit formats whose hardware conversion applies the sRGB transfer function.
bool isSrgbFormat(VkFormat format);

} // namespace raven::gfx
