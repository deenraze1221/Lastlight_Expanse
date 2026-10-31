#include <doctest/doctest.h>

#include "Renderer/Vulkan/DeviceCapabilities.h"
#include "Renderer/Vulkan/DeviceSelection.h"

#include <vector>

using namespace zodar::gfx;

namespace {

DeviceCandidate usable(VkPhysicalDeviceType type, VkDeviceSize memoryMiB) {
    return {"gpu", type, memoryMiB * 1024 * 1024, true};
}

DeviceCapabilities baselineCaps() {
    DeviceCapabilities c;
    c.apiVersion = VK_API_VERSION_1_3;
    c.graphicsFamily = 0;
    c.presentFamily = 0;
    c.dynamicRendering = c.synchronization2 = c.descriptorIndexing = c.timelineSemaphore = true;
    c.bufferDeviceAddress = c.drawIndirectCount = c.multiDrawIndirect = true;
    c.drawIndirectFirstInstance = c.shaderDrawParameters = true;
    return c;
}

} // namespace

TEST_CASE("device selection: type outranks memory, memory breaks ties") {
    const std::vector<DeviceCandidate> devices = {
        usable(VK_PHYSICAL_DEVICE_TYPE_CPU, 64000),
        usable(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, 2048),
        usable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 4096),
        usable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 12288),
    };
    CHECK(pickBestDevice(devices) == 3);
    const std::vector<DeviceCandidate> softwareOnly = {usable(VK_PHYSICAL_DEVICE_TYPE_CPU, 1024)};
    CHECK(pickBestDevice(softwareOnly) == 0);
}

TEST_CASE("device selection: devices below the baseline are never picked") {
    DeviceCandidate c = usable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 8192);
    c.meetsBaseline = false;
    CHECK(scoreDevice(c) < 0);
    const std::vector<DeviceCandidate> devices = {c};
    CHECK(pickBestDevice(devices) == -1);
    CHECK(pickBestDevice({}) == -1);
}

TEST_CASE("capabilities: baseline requirements are reported by name") {
    DeviceCapabilities c = baselineCaps();
    CHECK(missingBaseline(c, true).empty());

    c.bufferDeviceAddress = false;
    c.apiVersion = VK_API_VERSION_1_2;
    const auto missing = missingBaseline(c, true);
    REQUIRE(missing.size() == 2);
    CHECK(missing[0] == "Vulkan 1.3");
    CHECK(missing[1] == "bufferDeviceAddress");

    DeviceCapabilities headless = baselineCaps();
    headless.presentFamily = UINT32_MAX;
    CHECK(missingBaseline(headless, false).empty());
    CHECK(missingBaseline(headless, true).size() == 1);
}

TEST_CASE("capabilities: optional features resolve with fallbacks") {
    DeviceCapabilities c = baselineCaps();
    ResolvedFeatures f = resolveFeatures(c);
    CHECK_FALSE(f.meshShaders);
    CHECK_FALSE(f.rayTracing);
    CHECK_FALSE(f.asyncCompute);
    CHECK_FALSE(f.gpuTimestamps);
    for (const auto& e : f.entries()) {
        CHECK(e.fallback != nullptr); // Every optional feature names its fallback.
    }

    c.meshShader = true;
    c.accelerationStructure = true;
    c.rayQuery = true;
    c.asyncComputeFamily = 2;
    c.timestampQueries = true;
    c.timestampPeriodNs = 1.0f;
    c.samplerAnisotropy = true;
    c.maxSamplerAnisotropy = 16.0f;
    f = resolveFeatures(c);
    CHECK(f.meshShaders);
    CHECK(f.rayTracing);
    CHECK(f.asyncCompute);
    CHECK(f.gpuTimestamps);
    CHECK(f.anisotropicFiltering);

    c.rayQuery = false; // Acceleration structures alone are not enough.
    CHECK_FALSE(resolveFeatures(c).rayTracing);
}
