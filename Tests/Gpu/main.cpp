#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Core/Log.h"
#include "GpuTestContext.h"

#include <exception>

namespace raven::test {

GpuTestEnvironment& gpu() {
    static GpuTestEnvironment environment;
    return environment;
}

} // namespace raven::test

int main(int argc, char** argv) {
    raven::log::init({spdlog::level::warn, ""});
    int result = 1;
    try {
        auto& env = raven::test::gpu();
        env.context = std::make_unique<raven::gfx::VulkanContext>(raven::gfx::ContextDesc{"RavenGpuTests", true}, nullptr);
        env.upload = std::make_unique<raven::gfx::UploadContext>(*env.context);
        if (!env.context->validationEnabled()) {
            RAVEN_LOG_WARN("Validation layers unavailable: GPU tests run without validation checks");
        }

        doctest::Context doctest(argc, argv);
        result = doctest.run();

        env.upload.reset();
        env.context.reset();
    } catch (const std::exception& e) {
        RAVEN_LOG_ERROR("GPU test setup failed: {}", e.what());
        result = 1;
    }
    raven::log::shutdown();
    return result;
}
