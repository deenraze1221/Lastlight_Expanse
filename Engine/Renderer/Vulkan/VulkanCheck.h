#pragma once

#include <volk.h>

#include <source_location>

namespace raven::gfx {

const char* toString(VkResult result);

[[noreturn]] void throwVkError(VkResult result, const char* expression,
                               std::source_location location = std::source_location::current());

} // namespace raven::gfx

// For calls where anything other than VK_SUCCESS is fatal. Calls with meaningful
// non-success codes (acquire/present) are handled explicitly at the call site.
#define RAVEN_VK_CHECK(expr)                                  \
    do {                                                      \
        const VkResult raven_vk_result_ = (expr);             \
        if (raven_vk_result_ != VK_SUCCESS) {                 \
            ::raven::gfx::throwVkError(raven_vk_result_, #expr); \
        }                                                     \
    } while (false)
