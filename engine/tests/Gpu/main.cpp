#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Core/Log.h"
#include "GpuTestContext.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace zodar::test {

GpuTestEnvironment& gpu() {
    static GpuTestEnvironment environment;
    return environment;
}

namespace {
std::filesystem::path g_executableDir;
}

std::string shaderDir() {
    if (const char* env = std::getenv("ZODAR_SHADER_DIR")) {
        return env;
    }
    if (std::filesystem::exists(ZODAR_TEST_SHADER_DIR)) {
        return ZODAR_TEST_SHADER_DIR;
    }
    return (g_executableDir / "shaders").string();
}

} // namespace zodar::test

int main(int argc, char** argv) {
    zodar::log::init({spdlog::level::warn, ""});
    zodar::test::g_executableDir = std::filesystem::absolute(std::filesystem::path(argv[0])).parent_path();
    int result = 1;
    try {
        auto& env = zodar::test::gpu();
        env.context = std::make_unique<zodar::gfx::VulkanContext>(zodar::gfx::ContextDesc{"ZodarGpuTests", true, true}, nullptr);
        env.upload = std::make_unique<zodar::gfx::UploadContext>(*env.context);
        if (!env.context->validationEnabled()) {
            ZODAR_LOG_WARN("Validation layers unavailable: GPU tests run without validation checks");
        }
        std::printf("[ZodarGpuTests] device: %s | validation: %s\n", env.context->deviceInfo().name.c_str(),
                    env.context->validationEnabled() ? "core + synchronization" : "UNAVAILABLE");

        doctest::Context doctest(argc, argv);
        result = doctest.run();
        std::printf("[ZodarGpuTests] validation messages: %u errors, %u warnings\n",
                    env.context->validationErrorCount(), env.context->validationWarningCount());
        if (env.context->validationErrorCount() > 0) {
            result = 1;
        }

        env.upload.reset();
        env.context.reset();
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("GPU test setup failed: {}", e.what());
        result = 1;
    }
    zodar::log::shutdown();
    return result;
}
