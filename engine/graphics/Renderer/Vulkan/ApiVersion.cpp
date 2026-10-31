#include "Renderer/Vulkan/ApiVersion.h"

#include <algorithm>

namespace zodar::gfx {

InstanceApiChoice chooseInstanceApiVersion(std::optional<uint32_t> validationLayerVersion) {
    InstanceApiChoice choice{kMaximumApiVersion, "engine"};
    if (validationLayerVersion && apiMajorMinor(*validationLayerVersion) < choice.version) {
        choice = {apiMajorMinor(*validationLayerVersion), "validation layer"};
    }
    // Never request less than the minimum (a validation layer older than 1.3 is still enabled
    // and simply validates what it knows).
    choice.version = std::max(choice.version, kMinimumApiVersion);
    return choice;
}

uint32_t effectiveApiVersion(uint32_t instanceVersion, uint32_t deviceVersion) {
    return std::min(apiMajorMinor(instanceVersion), apiMajorMinor(deviceVersion));
}

std::string apiVersionString(uint32_t version, bool withPatch) {
    std::string s = std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version));
    if (withPatch) {
        s += "." + std::to_string(VK_API_VERSION_PATCH(version));
    }
    return s;
}

} // namespace zodar::gfx
