#include "Renderer/Vulkan/VulkanCheck.h"

#include "Core/Error.h"

#include <string>

namespace zodar::gfx {

const char* toString(VkResult result) {
    switch (result) {
#define ZODAR_CASE(name) case name: return #name
        ZODAR_CASE(VK_SUCCESS);
        ZODAR_CASE(VK_NOT_READY);
        ZODAR_CASE(VK_TIMEOUT);
        ZODAR_CASE(VK_EVENT_SET);
        ZODAR_CASE(VK_EVENT_RESET);
        ZODAR_CASE(VK_INCOMPLETE);
        ZODAR_CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
        ZODAR_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ZODAR_CASE(VK_ERROR_INITIALIZATION_FAILED);
        ZODAR_CASE(VK_ERROR_DEVICE_LOST);
        ZODAR_CASE(VK_ERROR_MEMORY_MAP_FAILED);
        ZODAR_CASE(VK_ERROR_LAYER_NOT_PRESENT);
        ZODAR_CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
        ZODAR_CASE(VK_ERROR_FEATURE_NOT_PRESENT);
        ZODAR_CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
        ZODAR_CASE(VK_ERROR_TOO_MANY_OBJECTS);
        ZODAR_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
        ZODAR_CASE(VK_ERROR_FRAGMENTED_POOL);
        ZODAR_CASE(VK_ERROR_UNKNOWN);
        ZODAR_CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
        ZODAR_CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
        ZODAR_CASE(VK_ERROR_FRAGMENTATION);
        ZODAR_CASE(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
        ZODAR_CASE(VK_PIPELINE_COMPILE_REQUIRED);
        ZODAR_CASE(VK_ERROR_SURFACE_LOST_KHR);
        ZODAR_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
        ZODAR_CASE(VK_SUBOPTIMAL_KHR);
        ZODAR_CASE(VK_ERROR_OUT_OF_DATE_KHR);
        ZODAR_CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
        ZODAR_CASE(VK_ERROR_VALIDATION_FAILED_EXT);
#undef ZODAR_CASE
    default:
        return "VK_RESULT_UNKNOWN";
    }
}

void throwVkError(VkResult result, const char* expression, std::source_location location) {
    fail(std::string(expression) + " returned " + toString(result), location);
}

} // namespace zodar::gfx
