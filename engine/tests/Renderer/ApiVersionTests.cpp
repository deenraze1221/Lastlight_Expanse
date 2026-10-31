#include <doctest/doctest.h>

#include "Renderer/Vulkan/ApiVersion.h"
#include "Renderer/Vulkan/DeviceCapabilities.h"

using namespace zodar::gfx;

namespace {
constexpr uint32_t v(uint32_t major, uint32_t minor, uint32_t patch = 0) {
    return VK_MAKE_API_VERSION(0, major, minor, patch);
}
} // namespace

TEST_CASE("api version: instance request is the engine maximum unless the validation layer is older") {
    CHECK(chooseInstanceApiVersion(std::nullopt).version == kMaximumApiVersion);
    CHECK(kMaximumApiVersion == v(1, 4));
    CHECK(std::string(chooseInstanceApiVersion(std::nullopt).limitedBy) == "engine");
    // An older validation layer limits validated runs (it cannot validate newer API usage).
    const InstanceApiChoice limited = chooseInstanceApiVersion(v(1, 3, 275));
    CHECK(limited.version == v(1, 3));
    CHECK(std::string(limited.limitedBy) == "validation layer");
    CHECK(chooseInstanceApiVersion(v(1, 4, 304)).version == v(1, 4));
    CHECK(chooseInstanceApiVersion(v(1, 5, 0)).version == v(1, 4));
    // Never below the minimum.
    CHECK(chooseInstanceApiVersion(v(1, 2, 198)).version == kMinimumApiVersion);
}

TEST_CASE("api version: effective version is the smaller major.minor") {
    CHECK(effectiveApiVersion(v(1, 4), v(1, 3, 290)) == v(1, 3));
    CHECK(effectiveApiVersion(v(1, 3), v(1, 4, 318)) == v(1, 3));
    CHECK(effectiveApiVersion(v(1, 4), v(1, 4, 318)) == v(1, 4));
    CHECK(apiVersionString(v(1, 4, 318)) == "1.4.318");
    CHECK(apiVersionString(v(1, 4, 318), false) == "1.4");
}

TEST_CASE("api version: Vulkan 1.4 is detected, never required") {
    DeviceCapabilities caps;
    caps.apiVersion = v(1, 3, 280);
    caps.effectiveApiVersion = v(1, 3);
    CHECK_FALSE(resolveFeatures(caps).vulkan14);
    caps.apiVersion = v(1, 4, 318);
    caps.effectiveApiVersion = v(1, 4);
    CHECK(resolveFeatures(caps).vulkan14);
    // 1.4 device behind a 1.3 instance (e.g. an older validation layer): 1.3 paths.
    caps.effectiveApiVersion = effectiveApiVersion(v(1, 3), caps.apiVersion);
    CHECK_FALSE(resolveFeatures(caps).vulkan14);
    // The baseline only needs 1.3.
    caps.apiVersion = v(1, 3, 0);
    bool versionMissing = false;
    for (const auto& m : missingBaseline(caps, false)) versionMissing |= m == "Vulkan 1.3";
    CHECK_FALSE(versionMissing);
}
