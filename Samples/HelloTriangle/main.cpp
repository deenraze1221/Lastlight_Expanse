// Milestone 1 sample: SDL3 window, Vulkan 1.3 device, swapchain and a rotating triangle.
//
//   HelloTriangle [--frames N] [--screenshot file.ppm] [--no-validation]
//                 [--present vsync|low-latency|immediate] [--width W] [--height H]
//
// Exit codes: 0 success, 1 fatal error, 2 Vulkan validation errors were reported,
//             3 a requested screenshot could not be written.

#include "Core/Error.h"
#include "Core/Log.h"
#include "Platform/Platform.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Options {
    uint64_t maxFrames = 0; // 0 = run until the window closes.
    std::optional<std::filesystem::path> screenshot;
    bool validation = true;
    raven::gfx::PresentMode presentMode = raven::gfx::PresentMode::VSync;
    uint32_t width = 1280;
    uint32_t height = 720;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                raven::fail("Missing value for " + std::string(arg));
            }
            return argv[++i];
        };
        if (arg == "--frames") {
            options.maxFrames = std::stoull(next());
        } else if (arg == "--screenshot") {
            options.screenshot = next();
        } else if (arg == "--no-validation") {
            options.validation = false;
        } else if (arg == "--width") {
            options.width = static_cast<uint32_t>(std::stoul(next()));
        } else if (arg == "--height") {
            options.height = static_cast<uint32_t>(std::stoul(next()));
        } else if (arg == "--present") {
            const std::string mode = next();
            if (mode == "vsync") options.presentMode = raven::gfx::PresentMode::VSync;
            else if (mode == "low-latency") options.presentMode = raven::gfx::PresentMode::LowLatency;
            else if (mode == "immediate") options.presentMode = raven::gfx::PresentMode::Immediate;
            else raven::fail("Unknown present mode '" + mode + "'");
        } else {
            raven::fail("Unknown argument '" + std::string(arg) + "'");
        }
    }
    return options;
}

std::filesystem::path shaderDirectory() {
    const char* base = SDL_GetBasePath(); // Directory of the executable, owned by SDL.
    return std::filesystem::path(base ? base : "") / "shaders";
}

struct PushConstants {
    float time;
    float aspect;
};

int run(const Options& options) {
    raven::Platform platform;
    raven::Window window({"RAVEN Engine - Hello Triangle", options.width, options.height, true});
    raven::gfx::Renderer renderer(window, {"HelloTriangle", options.validation, options.presentMode});
    const VkDevice device = renderer.context().device();

    const auto shaders = shaderDirectory();
    raven::gfx::ShaderModule vertex(device, raven::gfx::loadSpirv(shaders / "triangle.vert.spv"));
    raven::gfx::ShaderModule fragment(device, raven::gfx::loadSpirv(shaders / "triangle.frag.spv"));

    raven::gfx::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertex.handle();
    pipelineDesc.fragmentShader = fragment.handle();
    pipelineDesc.colorFormat = renderer.swapchain().format();
    pipelineDesc.pushConstantSize = sizeof(PushConstants);
    pipelineDesc.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
    raven::gfx::GraphicsPipeline pipeline(device, pipelineDesc);

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto statsStart = start;
    uint64_t statsFrames = 0;

    while (!window.shouldClose()) {
        window.pollEvents([&](const SDL_Event& event) {
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                window.requestClose();
            }
        });

        const bool lastFrame = options.maxFrames > 0 && renderer.frameNumber() + 1 >= options.maxFrames;
        if (lastFrame && options.screenshot) {
            renderer.requestScreenshot(*options.screenshot);
        }

        auto frame = renderer.beginFrame();
        if (!frame) {
            SDL_Delay(5); // Minimized or swapchain rebuilt; avoid a busy loop.
            continue;
        }

        const VkCommandBuffer cmd = frame->commandBuffer;
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = frame->imageView;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.015f, 0.017f, 0.022f, 1.0f}};

        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, frame->extent};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &rendering);

        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(frame->extent.width),
                                  static_cast<float>(frame->extent.height), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, frame->extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());

        const PushConstants push{std::chrono::duration<float>(Clock::now() - start).count(),
                                 static_cast<float>(frame->extent.width) / static_cast<float>(frame->extent.height)};
        vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);

        renderer.endFrame(*frame);

        ++statsFrames;
        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - statsStart).count();
        if (elapsed >= 2.0) {
            RAVEN_LOG_INFO("{:.1f} FPS ({:.2f} ms/frame)", statsFrames / elapsed, 1000.0 * elapsed / statsFrames);
            statsStart = now;
            statsFrames = 0;
        }
        if (lastFrame) {
            break;
        }
    }

    renderer.waitIdle();
    RAVEN_LOG_INFO("Rendered {} frames", renderer.frameNumber());

    const auto& context = renderer.context();
    if (context.validationEnabled() && context.validationErrorCount() > 0) {
        RAVEN_LOG_ERROR("{} Vulkan validation errors were reported", context.validationErrorCount());
        return 2;
    }
    if (options.screenshot && !renderer.lastScreenshotSucceeded()) {
        return 3;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    raven::log::init({spdlog::level::info, ""});
    RAVEN_LOG_INFO("RAVEN Engine {}.{}.{}", RAVEN_VERSION_MAJOR, RAVEN_VERSION_MINOR, RAVEN_VERSION_PATCH);

    int exitCode = EXIT_FAILURE;
    try {
        exitCode = run(parseOptions(argc, argv));
    } catch (const std::exception& e) {
        RAVEN_LOG_ERROR("Fatal: {}", e.what());
        exitCode = EXIT_FAILURE;
    }
    RAVEN_LOG_INFO("Shutdown complete (exit code {})", exitCode);
    raven::log::shutdown();
    return exitCode;
}
