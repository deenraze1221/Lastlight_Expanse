#include "GpuTestContext.h"

#include "Assets/Primitives.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/SceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <vector>

using namespace zodar;
using namespace zodar::gfx;
using zodar::test::gpu;

namespace {

struct GraphFixture {
    SceneRenderer scene{*gpu().context, *gpu().upload, {zodar::test::shaderDir()}};
    RenderGraphResources graphResources{*gpu().context, scene.bindless()};
    FrameAllocator scratch{*gpu().context, 2};
    GpuMesh cube, sphere, plane;
    uint64_t frame = 0;

    GraphFixture() {
        UploadBatch batch(*gpu().upload);
        cube = GpuMesh(batch, primitives::cube(1.0f));
        sphere = GpuMesh(batch, primitives::uvSphere(1.0f, 48, 24));
        plane = GpuMesh(batch, primitives::plane(1.0f, 1.0f));
        batch.submit();
    }

    GpuImage makeOutput(VkExtent2D extent) {
        return GpuImage(*gpu().context, {extent.width, extent.height, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "Out"});
    }

    FrameInfo begin(VkCommandBuffer cmd) {
        return {cmd, static_cast<uint32_t>(frame % 2), frame, &scratch, nullptr};
    }

    // Reference path: SceneRenderer::render + resolve with hand-written barriers.
    std::vector<uint8_t> renderReference(const SceneView& view, VkExtent2D extent) {
        GpuImage out = makeOutput(extent);
        scratch.beginFrame(static_cast<uint32_t>(frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            const FrameInfo info = begin(cmd);
            scene.render(info, extent, view);
            transitionImage(cmd, out.handle(), out.fullRange(), VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            scene.resolve(info, out.view(), VK_FORMAT_R8G8B8A8_UNORM, extent);
            transitionImage(cmd, out.handle(), out.fullRange(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT);
        });
        ++frame;
        return gpu().upload->readImage(out, VK_IMAGE_LAYOUT_GENERAL);
    }

    // Render-graph path.
    std::vector<uint8_t> renderGraph(const SceneView& view, VkExtent2D extent, RenderStats* stats = nullptr,
                                     size_t* passes = nullptr) {
        GpuImage out = makeOutput(extent);
        scratch.beginFrame(static_cast<uint32_t>(frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            const FrameInfo info = begin(cmd);
            RenderGraph graph(graphResources);
            RGImportedImage imported;
            imported.image = out.handle();
            imported.view = out.view();
            imported.format = out.format();
            imported.extent = extent;
            imported.final = AccessState{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                                         VK_IMAGE_LAYOUT_GENERAL};
            const RGImage output = graph.importImage("Output", imported);
            scene.addToGraph(graph, info, extent, view, output, VK_FORMAT_R8G8B8A8_UNORM, stats);
            graph.execute(info);
            if (passes) *passes = graph.compile().passes.size();
        });
        ++frame;
        return gpu().upload->readImage(out, VK_IMAGE_LAYOUT_GENERAL);
    }

    MaterialHandle material(glm::vec4 color, bool unlit, AlphaMode alpha = AlphaMode::Opaque) {
        MaterialDesc d;
        d.shader = unlit ? scene.materials().unlitShader() : scene.materials().pbrShader();
        d.baseColorFactor = color;
        d.alphaMode = alpha;
        d.roughness = 0.4f;
        return scene.materials().createMaterial(d);
    }
};

SceneView camera() {
    SceneView view;
    view.camera.position = {1.5f, 1.2f, 4.5f};
    view.camera.lookAt({0, 0, 0});
    view.clearColor = {0.05f, 0.06f, 0.08f, 1};
    view.lighting.pointLights = {{{2, 2, 2}, {1.0f, 0.8f, 0.6f}, 15.0f, 10.0f}};
    return view;
}

} // namespace

TEST_CASE("render graph: output is pixel-identical to the reference path") {
    zodar::test::ValidationGuard guard;
    GraphFixture f;
    const glm::mat4 faceCamera = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(1, 0, 0));
    std::vector<MeshDraw> draws = {
        {&f.sphere, 0, f.material({0.8f, 0.2f, 0.2f, 1}, false), glm::mat4(1)},
        {&f.cube, 0, f.material({0.2f, 0.7f, 0.9f, 1}, true), glm::translate(glm::mat4(1), {-1.5f, 0, -1})},
        {&f.plane, 0, f.material({0.4f, 0.4f, 0.4f, 1}, false), glm::scale(glm::translate(glm::mat4(1), {0, -1, 0}), glm::vec3(6))},
        {&f.plane, 0, f.material({0.2f, 1.0f, 0.3f, 0.5f}, true, AlphaMode::Blend),
         glm::translate(glm::mat4(1), {0.5f, 0, 1.5f}) * glm::scale(faceCamera, glm::vec3(1.5f))},
    };
    SceneView view = camera();
    view.draws = draws;

    for (VkExtent2D extent : {VkExtent2D{96, 64}, VkExtent2D{64, 64}}) {
        const auto reference = f.renderReference(view, extent);
        RenderStats stats;
        size_t passes = 0;
        const auto graph = f.renderGraph(view, extent, &stats, &passes);
        CHECK(passes == 2);
        CHECK(stats.drawCalls == 4);
        CHECK(stats.triangles > 0);
        REQUIRE(reference.size() == graph.size());
        size_t different = 0;
        for (size_t i = 0; i < reference.size(); ++i) different += reference[i] != graph[i];
        CHECK(different == 0);
    }
}

TEST_CASE("render graph: chained transient passes, culling and pooled memory") {
    zodar::test::ValidationGuard guard;
    GraphFixture f;
    const VkExtent2D extent{32, 32};
    GpuImage out = f.makeOutput(extent);

    const auto runFrame = [&](bool includeUnused, size_t* executed, size_t* culled) {
        f.scratch.beginFrame(static_cast<uint32_t>(f.frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            const FrameInfo info = f.begin(cmd);
            RenderGraph graph(f.graphResources);
            RGImportedImage imported;
            imported.image = out.handle();
            imported.view = out.view();
            imported.format = out.format();
            imported.extent = extent;
            imported.final = AccessState{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                                         VK_IMAGE_LAYOUT_GENERAL};
            const RGImage output = graph.importImage("Output", imported);

            // A: clear a transient to a colour. B: sample it into a second transient.
            // C: resolve B into the output. D: writes a transient nobody reads (culled).
            const RGImage a = graph.createImage("A", {extent.width, extent.height, VK_FORMAT_R16G16B16A16_SFLOAT});
            const RGImage b = graph.createImage("B", {extent.width, extent.height, VK_FORMAT_R16G16B16A16_SFLOAT});
            graph.addPass("A", RenderGraph::PassType::Raster,
                          [&](PassBuilder& p) { p.colorAttachment(a, LoadOp::Clear, {0.25f, 0.5f, 0.75f, 1.0f}); },
                          [](PassContext&) {});
            graph.addPass("B", RenderGraph::PassType::Raster,
                          [&](PassBuilder& p) {
                              p.read(a, ResourceUsage::SampledFragment);
                              p.colorAttachment(b, LoadOp::DontCare);
                          },
                          [](PassContext&) { FAIL("B is culled: its output is never read"); });
            if (includeUnused) {
                const RGImage unused = graph.createImage("Unused", {extent.width, extent.height, VK_FORMAT_R8G8B8A8_UNORM});
                graph.addPass("Unused", RenderGraph::PassType::Raster,
                              [&](PassBuilder& p) { p.colorAttachment(unused, LoadOp::Clear); },
                              [](PassContext&) { FAIL("culled pass executed"); });
            }
            graph.addPass("C", RenderGraph::PassType::Raster,
                          [&](PassBuilder& p) {
                              p.read(a, ResourceUsage::SampledFragment);
                              p.colorAttachment(output, LoadOp::Clear, {1, 0, 0, 1});
                          },
                          [](PassContext&) {});
            graph.execute(info);
            *executed = graph.compile().passes.size();
            *culled = graph.compile().culled.size();
        });
        ++f.frame;
    };

    size_t executed = 0, culled = 0;
    runFrame(true, &executed, &culled);
    CHECK(executed == 2); // A and C; B only writes an unread transient, Unused likewise.
    CHECK(culled == 2);
    const size_t pooled = f.graphResources.imageCount();
    CHECK(pooled == 1);   // Only A was ever allocated.
    runFrame(false, &executed, &culled);
    CHECK(f.graphResources.imageCount() == pooled); // Reused, not reallocated.

    const auto pixels = gpu().upload->readImage(out, VK_IMAGE_LAYOUT_GENERAL);
    CHECK(pixels[0] == 255); // C cleared the output.

    // Images unused for several frames are released through the deletion queue.
    for (int i = 0; i < 12; ++i) {
        f.scratch.beginFrame(static_cast<uint32_t>(f.frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            RenderGraph graph(f.graphResources);
            graph.execute(f.begin(cmd));
        });
        ++f.frame;
    }
    CHECK(f.graphResources.imageCount() == 0);
}

#include "Renderer/GpuProfiler.h"

TEST_CASE("gpu profiler: per-pass timestamps from the render graph and memory stats") {
    zodar::test::ValidationGuard guard;
    GraphFixture f;
    GpuProfiler profiler(*gpu().context, 2);
    if (!profiler.enabled()) {
        MESSAGE("Device has no timestamp support; profiler disabled (fallback: CPU timers)");
        return;
    }
    std::vector<MeshDraw> draws;
    const MaterialHandle m = f.material({0.7f, 0.7f, 0.7f, 1}, false);
    for (int i = 0; i < 50; ++i) {
        draws.push_back({&f.sphere, 0, m, glm::translate(glm::mat4(1), {(i % 10) * 0.3f - 1.5f, (i / 10) * 0.3f - 0.6f, 0})});
    }
    SceneView view = camera();
    view.draws = draws;
    const VkExtent2D extent{128, 128};
    GpuImage out = f.makeOutput(extent);

    for (int i = 0; i < 3; ++i) {
        const uint32_t slot = static_cast<uint32_t>(f.frame % 2);
        f.scratch.beginFrame(slot);
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            FrameInfo info = f.begin(cmd);
            info.profiler = &profiler;
            profiler.beginFrame(cmd, slot);
            const uint32_t frameScope = profiler.beginScope(cmd, "Frame");
            RenderGraph graph(f.graphResources);
            RGImportedImage imported;
            imported.image = out.handle();
            imported.view = out.view();
            imported.format = out.format();
            imported.extent = extent;
            const RGImage output = graph.importImage("Output", imported);
            f.scene.addToGraph(graph, info, extent, view, output, out.format());
            graph.execute(info);
            profiler.endScope(cmd, frameScope);
        });
        ++f.frame;
    }
    // Frame 0's results were collected when slot 0 was reused by frame 2.
    const auto& results = profiler.lastResults();
    REQUIRE(results.size() == 3);
    CHECK(results[0].name == "Frame");
    CHECK(results[1].name == "Scene");
    CHECK(results[1].depth == 1);
    CHECK(results[2].name == "Resolve");
    CHECK(results[1].ms > 0.0);
    CHECK(results[0].ms >= results[1].ms);
    CHECK(profiler.lastFrameMs() == doctest::Approx(results[0].ms));

    const GpuMemoryStats memory = queryMemoryStats(*gpu().context);
    CHECK(memory.allocationCount > 0);
    CHECK(memory.allocationBytes > 0);
    CHECK(memory.deviceLocalBudget >= memory.deviceLocalUsage);
}
