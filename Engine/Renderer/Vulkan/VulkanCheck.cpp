#include "Renderer/Vulkan/VulkanCheck.h"

#include "Core/Error.h"

#include <string>

namespace raven::gfx {

const char* toString(VkResult result) {
    switch (result) {
#define RAVEN_CASE(name) case name: return #name
        RAVEN_CASE(VK_SUCCESS);
        RAVEN_CASE(VK_NOT_READY);
        RAVEN_CASE(VK_TIMEOUT);
        RAVEN_CASE(VK_EVENT_SET);
        RAVEN_CASE(VK_EVENT_RESET);
        RAVEN_CASE(VK_INCOMPLETE);
        RAVEN_CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
        RAVEN_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        RAVEN_CASE(VK_ERROR_INITIALIZATION_FAILED);
        RAVEN_CASE(VK_ERROR_DEVICE_LOST);
        RAVEN_CASE(VK_ERROR_MEMORY_MAP_FAILED);
        RAVEN_CASE(VK_ERROR_LAYER_NOT_PRESENT);
        RAVEN_CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
        RAVEN_CASE(VK_ERROR_FEATURE_NOT_PRESENT);
        RAVEN_CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
        RAVEN_CASE(VK_ERROR_TOO_MANY_OBJECTS);
        RAVEN_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
        RAVEN_CASE(VK_ERROR_FRAGMENTED_POOL);
        RAVEN_CASE(VK_ERROR_UNKNOWN);
        RAVEN_CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
        RAVEN_CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
        RAVEN_CASE(VK_ERROR_FRAGMENTATION);
        RAVEN_CASE(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
        RAVEN_CASE(VK_PIPELINE_COMPILE_REQUIRED);
        RAVEN_CASE(VK_ERROR_SURFACE_LOST_KHR);
        RAVEN_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
        RAVEN_CASE(VK_SUBOPTIMAL_KHR);
        RAVEN_CASE(VK_ERROR_OUT_OF_DATE_KHR);
        RAVEN_CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
        RAVEN_CASE(VK_ERROR_VALIDATION_FAILED_EXT);
#undef RAVEN_CASE
    default:
        return "VK_RESULT_UNKNOWN";
    }
}

void throwVkError(VkResult result, const char* expression, std::source_location location) {
    fail(std::string(expression) + " returned " + toString(result), location);
}

} // namespace raven::gfx
