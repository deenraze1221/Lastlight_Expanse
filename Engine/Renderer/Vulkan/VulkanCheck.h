#pragma once

#include <volk.h>

#include <source_location>

namespace zodar::gfx {

const char* toString(VkResult result);

[[noreturn]] void throwVkError(VkResult result, const char* expression,
                               std::source_location location = std::source_location::current());

} // namespace zodar::gfx

// For calls where anything other than VK_SUCCESS is fatal. Calls with meaningful
// non-success codes (acquire/present) are handled explicitly at the call site.
#define ZODAR_VK_CHECK(expr)                                  \
    do {                                                      \
        const VkResult zodar_vk_result_ = (expr);             \
        if (zodar_vk_result_ != VK_SUCCESS) {                 \
            ::zodar::gfx::throwVkError(zodar_vk_result_, #expr); \
        }                                                     \
    } while (false)
