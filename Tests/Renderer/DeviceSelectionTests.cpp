#include <doctest/doctest.h>

#include "Renderer/Vulkan/DeviceSelection.h"

#include <vector>

using raven::gfx::DeviceCandidate;

namespace {

DeviceCandidate suitable(VkPhysicalDeviceType type, VkDeviceSize memoryMiB) {
    DeviceCandidate c;
    c.type = type;
    c.apiVersion = VK_API_VERSION_1_3;
    c.hasGraphicsQueue = c.hasPresentQueue = c.hasSwapchainExtension = true;
    c.hasDynamicRendering = c.hasSynchronization2 = true;
    c.deviceLocalMemoryBytes = memoryMiB * 1024 * 1024;
    return c;
}

} // namespace

TEST_CASE("device selection: requirements are mandatory") {
    auto c = suitable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 8192);
    CHECK(raven::gfx::scoreDevice(c) > 0);

    auto old = c;
    old.apiVersion = VK_API_VERSION_1_2;
    CHECK(raven::gfx::scoreDevice(old) < 0);

    auto noPresent = c;
    noPresent.hasPresentQueue = false;
    CHECK(raven::gfx::scoreDevice(noPresent) < 0);

    auto noDynamic = c;
    noDynamic.hasDynamicRendering = false;
    CHECK(raven::gfx::scoreDevice(noDynamic) < 0);

    auto noSync2 = c;
    noSync2.hasSynchronization2 = false;
    CHECK(raven::gfx::scoreDevice(noSync2) < 0);
}

TEST_CASE("device selection: type outranks memory, memory breaks ties") {
    const std::vector<DeviceCandidate> devices = {
        suitable(VK_PHYSICAL_DEVICE_TYPE_CPU, 64000),
        suitable(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, 2048),
        suitable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 4096),
        suitable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 12288),
    };
    CHECK(raven::gfx::pickBestDevice(devices) == 3);

    const std::vector<DeviceCandidate> softwareOnly = {suitable(VK_PHYSICAL_DEVICE_TYPE_CPU, 1024)};
    CHECK(raven::gfx::pickBestDevice(softwareOnly) == 0);
}

TEST_CASE("device selection: no suitable device") {
    auto c = suitable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 8192);
    c.hasSwapchainExtension = false;
    const std::vector<DeviceCandidate> devices = {c};
    CHECK(raven::gfx::pickBestDevice(devices) == -1);
    CHECK(raven::gfx::pickBestDevice({}) == -1);
}
