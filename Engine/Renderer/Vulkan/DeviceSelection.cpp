#include "Renderer/Vulkan/DeviceSelection.h"

namespace raven::gfx {

int64_t scoreDevice(const DeviceCandidate& c) {
    if (c.apiVersion < VK_API_VERSION_1_3 || !c.hasGraphicsQueue || !c.hasPresentQueue ||
        !c.hasSwapchainExtension || !c.hasDynamicRendering || !c.hasSynchronization2) {
        return -1;
    }
    int64_t score = 0;
    switch (c.type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score = 4'000'000; break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 3'000'000; break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score = 2'000'000; break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            score = 1'000'000; break;
    default:                                     score = 0; break;
    }
    // Memory in MiB, capped so it never outranks the device type.
    const int64_t memoryMiB = static_cast<int64_t>(c.deviceLocalMemoryBytes / (1024 * 1024));
    return score + (memoryMiB < 999'999 ? memoryMiB : 999'999);
}

int pickBestDevice(std::span<const DeviceCandidate> candidates) {
    int best = -1;
    int64_t bestScore = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const int64_t score = scoreDevice(candidates[i]);
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return bestScore < 0 ? -1 : best;
}

} // namespace raven::gfx
