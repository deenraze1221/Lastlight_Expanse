#include "GpuTestContext.h"

#include "Debug/DebugOverlay.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"

using namespace raven;
using namespace raven::gfx;
using raven::test::gpu;

TEST_CASE("debug overlay: profiler window renders headless through the render graph") {
    raven::test::ValidationGuard guard;
    BindlessRegistry bindless(*gpu().context);
    RenderGraphResources graphResources(*gpu().context, bindless);
    const VkExtent2D extent{480, 360};
    GpuImage out(*gpu().context, {extent.width, extent.height, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "Out"});

    debug::DebugOverlay overlay(*gpu().context, nullptr, VK_FORMAT_R8G8B8A8_UNORM, 2);
    CpuProfiler cpu;
    History cpuMs, gpuMs;
    for (float v : {16.6f, 16.9f, 15.2f}) cpuMs.push(v);

    size_t lastPassCount = 0;
    for (uint64_t frame = 0; frame < 3; ++frame) { // Several frames: font upload + steady state.
        cpu.beginFrame();
        { RAVEN_CPU_SCOPE(cpu, "Update"); }
        cpu.endFrame();
        overlay.newFrame(extent, 1.0f / 60.0f);
        debug::ProfilerSnapshot snapshot;
        snapshot.cpu = &cpu;
        snapshot.cpuFrameMs = &cpuMs;
        snapshot.gpuFrameMs = &gpuMs;
        snapshot.resolution = extent;
        snapshot.frameNumber = frame;
        snapshot.render.drawCalls = 12;
        snapshot.memory = queryMemoryStats(*gpu().context);
        overlay.drawProfiler(snapshot);

        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            RenderGraph graph(graphResources);
            RGImportedImage imported;
            imported.image = out.handle();
            imported.view = out.view();
            imported.format = out.format();
            imported.extent = extent;
            imported.final = AccessState{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                                         VK_IMAGE_LAYOUT_GENERAL};
            const RGImage target = graph.importImage("Output", imported);
            graph.addPass("Clear", RenderGraph::PassType::Raster,
                          [&](PassBuilder& b) { b.colorAttachment(target, LoadOp::Clear, {0, 0, 0, 1}); },
                          [](PassContext&) {});
            overlay.addToGraph(graph, target);
            graph.execute({cmd, static_cast<uint32_t>(frame % 2), frame, nullptr, nullptr});
            lastPassCount = graph.compile().passes.size();
        });
    }
    // ImGui hides auto-sized windows on their first frame; afterwards the overlay pass runs.
    CHECK(lastPassCount == 2);
    const auto pixels = gpu().upload->readImage(out, VK_IMAGE_LAYOUT_GENERAL);
    size_t drawn = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        drawn += (pixels[i] | pixels[i + 1] | pixels[i + 2]) != 0;
    }
    CHECK(drawn > 10000);                     // The window covers a large area.
    CHECK(pixels[(350 * extent.width + 470) * 4] == 0); // Bottom-right corner untouched.
}
