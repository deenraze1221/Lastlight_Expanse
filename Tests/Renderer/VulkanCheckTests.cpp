#include <doctest/doctest.h>

#include "Core/Error.h"
#include "Renderer/Vulkan/VulkanCheck.h"

#include <string>

TEST_CASE("vulkan check: result names and throwing") {
    CHECK(std::string(zodar::gfx::toString(VK_SUCCESS)) == "VK_SUCCESS");
    CHECK(std::string(zodar::gfx::toString(VK_ERROR_DEVICE_LOST)) == "VK_ERROR_DEVICE_LOST");
    CHECK_NOTHROW(ZODAR_VK_CHECK(VK_SUCCESS));
    try {
        ZODAR_VK_CHECK(VK_ERROR_OUT_OF_DATE_KHR);
        FAIL("no throw");
    } catch (const zodar::Error& e) {
        CHECK(std::string(e.what()).find("VK_ERROR_OUT_OF_DATE_KHR") != std::string::npos);
    }
}
