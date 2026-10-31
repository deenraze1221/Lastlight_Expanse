#pragma once

#include <volk.h>

#include <cstdint>
#include <optional>
#include <string>

namespace zodar::gfx {

// Vulkan API version policy. Vulkan 1.3 is the required minimum; 1.4 is used when the
// loader, the device and (if enabled) the validation layer all support it. Nothing in the
// engine requires 1.4 yet: on a 1.4 device the effective version only allows later code to
// opt into promoted 1.4 functionality behind ResolvedFeatures::vulkan14 with 1.3 fallbacks.
// Pure functions (unit tested); versions compare by major.minor (patch ignored).
constexpr uint32_t kMinimumApiVersion = VK_API_VERSION_1_3;
constexpr uint32_t kMaximumApiVersion = VK_API_VERSION_1_4; // newest version the engine is written against

// major.minor with variant and patch cleared.
constexpr uint32_t apiMajorMinor(uint32_t version) {
    return VK_MAKE_API_VERSION(0, VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), 0);
}

struct InstanceApiChoice {
    uint32_t version = kMinimumApiVersion; // VkApplicationInfo::apiVersion
    const char* limitedBy = "engine";      // "engine" or "validation layer"
};

// Instance version to request: the engine maximum, limited to the validation layer's spec
// version when the layer is enabled (an older layer cannot validate newer API usage).
//
// The loader version does not limit the request: loaders >= 1.1 accept any apiVersion, and
// vkEnumerateInstanceVersion only bounds *instance-level* functionality, of which the engine
// uses nothing newer than 1.3. Device-level functionality is bounded by
// effectiveApiVersion(request, device). The loader must still be >= kMinimumApiVersion.
InstanceApiChoice chooseInstanceApiVersion(std::optional<uint32_t> validationLayerVersion);

// Version the engine may use on a device: min(instance request, device apiVersion), major.minor.
uint32_t effectiveApiVersion(uint32_t instanceVersion, uint32_t deviceVersion);

// "1.3.275" (patch shown when non-zero, "1.4" style otherwise via apiVersionString(v, false)).
std::string apiVersionString(uint32_t version, bool withPatch = true);

} // namespace zodar::gfx
