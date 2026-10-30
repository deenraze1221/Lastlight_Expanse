#include <doctest/doctest.h>

#include "Renderer/Vulkan/SwapchainSupport.h"

#include <vector>

using namespace zodar::gfx;

TEST_CASE("swapchain: prefers sRGB BGRA, falls back to first format") {
    const std::vector<VkSurfaceFormatKHR> formats = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    CHECK(chooseSurfaceFormat(formats).format == VK_FORMAT_B8G8R8A8_SRGB);

    const std::vector<VkSurfaceFormatKHR> rgbaOnly = {{VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    CHECK(chooseSurfaceFormat(rgbaOnly).format == VK_FORMAT_R8G8B8A8_SRGB);

    const std::vector<VkSurfaceFormatKHR> unorm = {{VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    CHECK(chooseSurfaceFormat(unorm).format == VK_FORMAT_A2B10G10R10_UNORM_PACK32);
}

TEST_CASE("swapchain: present mode preference with FIFO fallback") {
    const std::vector<VkPresentModeKHR> all = {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                                               VK_PRESENT_MODE_FIFO_KHR};
    const std::vector<VkPresentModeKHR> fifoOnly = {VK_PRESENT_MODE_FIFO_KHR};
    CHECK(choosePresentMode(all, PresentMode::VSync) == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(choosePresentMode(all, PresentMode::LowLatency) == VK_PRESENT_MODE_MAILBOX_KHR);
    CHECK(choosePresentMode(all, PresentMode::Immediate) == VK_PRESENT_MODE_IMMEDIATE_KHR);
    CHECK(choosePresentMode(fifoOnly, PresentMode::Immediate) == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(choosePresentMode(fifoOnly, PresentMode::LowLatency) == VK_PRESENT_MODE_FIFO_KHR);
}

TEST_CASE("swapchain: extent uses surface size or clamps the framebuffer") {
    VkSurfaceCapabilitiesKHR caps{};
    caps.currentExtent = {800, 600};
    CHECK(chooseExtent(caps, {1920, 1080}).width == 800);

    caps.currentExtent = {UINT32_MAX, UINT32_MAX};
    caps.minImageExtent = {1, 1};
    caps.maxImageExtent = {1600, 900};
    const VkExtent2D clamped = chooseExtent(caps, {1920, 1080});
    CHECK(clamped.width == 1600);
    CHECK(clamped.height == 900);
}

TEST_CASE("swapchain: image count is min+1 within limits") {
    VkSurfaceCapabilitiesKHR caps{};
    caps.minImageCount = 2;
    caps.maxImageCount = 0; // Unlimited.
    CHECK(chooseImageCount(caps) == 3);
    caps.maxImageCount = 2;
    CHECK(chooseImageCount(caps) == 2);
}
