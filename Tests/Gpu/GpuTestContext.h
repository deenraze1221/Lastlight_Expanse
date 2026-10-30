#pragma once

#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>

namespace raven::test {

// One headless Vulkan device shared by all GPU tests (created in main).
struct GpuTestEnvironment {
    std::unique_ptr<gfx::VulkanContext> context;
    std::unique_ptr<gfx::UploadContext> upload;
};

GpuTestEnvironment& gpu();

// Compiled SPIR-V directory: $RAVEN_SHADER_DIR, else the build-time path if it exists,
// else "shaders" next to the test executable (packaged builds).
std::string shaderDir();

// Fails the current test if validation reported errors since the guard was created.
class ValidationGuard {
public:
    ValidationGuard() : m_errors(gpu().context->validationErrorCount()) {}
    ~ValidationGuard() { CHECK(gpu().context->validationErrorCount() == m_errors); }

private:
    uint32_t m_errors;
};

} // namespace raven::test
