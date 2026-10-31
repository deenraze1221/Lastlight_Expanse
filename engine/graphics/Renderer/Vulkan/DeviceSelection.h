#pragma once

#include <volk.h>

#include <cstdint>
#include <span>
#include <string>

namespace zodar::gfx {

// Hardware-independent summary of a physical device used for ranking.
struct DeviceCandidate {
    std::string name;
    VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    VkDeviceSize deviceLocalMemoryBytes = 0;
    bool meetsBaseline = false; // See missingBaseline() in DeviceCapabilities.h.
};

// Negative when unusable. Discrete > integrated > virtual > CPU; memory breaks ties.
int64_t scoreDevice(const DeviceCandidate& candidate);

// Index of the best usable candidate, or -1.
int pickBestDevice(std::span<const DeviceCandidate> candidates);

} // namespace zodar::gfx
