#pragma once

#include <volk.h>

#include <cstdint>
#include <span>
#include <string>

namespace raven::gfx {

// Hardware-independent description of a physical device, so selection logic is testable.
struct DeviceCandidate {
    std::string name;
    VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    uint32_t apiVersion = 0;
    bool hasGraphicsQueue = false;
    bool hasPresentQueue = false;
    bool hasSwapchainExtension = false;
    bool hasDynamicRendering = false;
    bool hasSynchronization2 = false;
    bool hasDescriptorIndexing = false; // Runtime arrays, partially bound, update-after-bind, non-uniform indexing.
    VkDeviceSize deviceLocalMemoryBytes = 0;
};

// Returns a negative score when the device cannot run the engine (Vulkan 1.3 with
// dynamic rendering, synchronization2, descriptor indexing, swapchain and a presentable
// graphics queue; the last two are reported as satisfied for headless contexts).
// Discrete > integrated > virtual > CPU; memory breaks ties within a type.
int64_t scoreDevice(const DeviceCandidate& candidate);

// Index of the best suitable candidate, or -1 when none is suitable.
int pickBestDevice(std::span<const DeviceCandidate> candidates);

} // namespace raven::gfx
