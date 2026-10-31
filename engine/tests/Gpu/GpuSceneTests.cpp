#include "GpuTestContext.h"

#include "Assets/Primitives.h"
#include "Renderer/Frustum.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/GpuProfiler.h"
#include "Renderer/GpuScene.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/SceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <vector>

using namespace zodar;
using namespace zodar::gfx;
using zodar::test::gpu;

namespace {

struct Fixture {
    SceneRenderer scene{*gpu().context, *gpu().upload, {zodar::test::shaderDir()}};
    RenderGraphResources graphResources{*gpu().context, scene.bindless()};
    FrameAllocator scratch{*gpu().context, 2};
    std::vector<GpuMesh> meshes;
    uint64_t frame = 0;

    Fixture() {
        UploadBatch batch(*gpu().upload);
        meshes.reserve(4);
        meshes.emplace_back(batch, primitives::cube(1.0f), &scene.geometry());
        meshes.emplace_back(batch, primitives::uvSphere(0.6f, 24, 12), &scene.geometry());
        meshes.emplace_back(batch, primitives::plane(1.0f, 1.0f), &scene.geometry());
        meshes.emplace_back(batch, primitives::uvSphere(0.3f, 9, 7), &scene.geometry());
        batch.submit();
    }

    MaterialHandle material(glm::vec4 color, bool unlit, AlphaMode alpha = AlphaMode::Opaque, bool twoSided = false) {
        MaterialDesc d;
        d.shader = unlit ? scene.materials().unlitShader() : scene.materials().pbrShader();
        d.baseColorFactor = color;
        d.alphaMode = alpha;
        d.doubleSided = twoSided;
        d.roughness = 0.35f;
        return scene.materials().createMaterial(d);
    }

    // Renders through the render graph; draws come either from `view.draws` (CPU path)
    // or from `view.gpuScene` (GPU-driven path).
    std::vector<uint8_t> render(const SceneView& view, VkExtent2D extent, RenderStats* stats = nullptr,
                                GpuProfiler* profiler = nullptr) {
        GpuImage out(*gpu().context, {extent.width, extent.height, VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "Out"});
        scratch.beginFrame(static_cast<uint32_t>(frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            if (profiler) profiler->beginFrame(cmd, static_cast<uint32_t>(frame % 2));
            const FrameInfo info{cmd, static_cast<uint32_t>(frame % 2), frame, &scratch, profiler};
            RenderGraph graph(graphResources);
            RGImportedImage imported;
            imported.image = out.handle();
            imported.view = out.view();
            imported.format = out.format();
            imported.extent = extent;
            imported.final = AccessState{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                                         VK_IMAGE_LAYOUT_GENERAL};
            const RGImage output = graph.importImage("Output", imported);
            scene.addToGraph(graph, info, extent, view, output, out.format(), stats);
            graph.execute(info);
        });
        ++frame;
        return gpu().upload->readImage(out, VK_IMAGE_LAYOUT_GENERAL);
    }
};

size_t differentBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    REQUIRE(a.size() == b.size());
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

// A mixed scene: several pipelines and materials, instancing, masked/two-sided/blended
// materials, and objects outside the view.
std::vector<MeshDraw> buildScene(Fixture& f) {
    const MaterialHandle red = f.material({0.8f, 0.2f, 0.2f, 1}, false);
    const MaterialHandle blue = f.material({0.2f, 0.4f, 0.9f, 1}, false);
    const MaterialHandle glow = f.material({0.1f, 0.9f, 0.5f, 1}, true);
    const MaterialHandle twoSided = f.material({0.9f, 0.8f, 0.2f, 1}, false, AlphaMode::Opaque, true);
    const MaterialHandle masked = f.material({0.6f, 0.6f, 0.6f, 1}, true, AlphaMode::Mask);
    const MaterialHandle glass = f.material({0.3f, 0.8f, 1.0f, 0.4f}, false, AlphaMode::Blend);
    const MaterialHandle glass2 = f.material({1.0f, 0.3f, 0.6f, 0.5f}, true, AlphaMode::Blend);
    std::vector<MeshDraw> draws;
    for (int z = 0; z < 8; ++z) {
        for (int x = 0; x < 8; ++x) {
            const int i = z * 8 + x;
            const MaterialHandle m = i % 5 == 0 ? glow : i % 3 == 0 ? blue : i % 7 == 0 ? masked : red;
            draws.push_back({&f.meshes[static_cast<size_t>(i % 2)], 0, m,
                             glm::translate(glm::mat4(1), {x * 1.5f - 5.25f, 0.6f, z * -1.5f})});
        }
    }
    draws.push_back({&f.meshes[2], 0, twoSided,
                     glm::scale(glm::translate(glm::mat4(1), {0, 0, -5}), glm::vec3(16.0f))}); // Ground
    for (int i = 0; i < 20; ++i) { // Far outside the view: must be culled by both paths.
        draws.push_back({&f.meshes[0], 0, red, glm::translate(glm::mat4(1), {200.0f + i, 0, 0})});
    }
    draws.push_back({&f.meshes[1], 0, glass, glm::translate(glm::mat4(1), {-1.0f, 1.5f, 1.0f})});
    draws.push_back({&f.meshes[3], 0, glass2, glm::translate(glm::mat4(1), {0.8f, 1.2f, 2.0f})});
    return draws;
}

SceneView cameraAt(glm::vec3 position, glm::vec3 target) {
    SceneView view;
    view.camera.position = position;
    view.camera.lookAt(target);
    view.clearColor = {0.05f, 0.06f, 0.08f, 1};
    view.lighting.pointLights = {{{0, 3, 2}, {1.0f, 0.85f, 0.7f}, 25.0f, 15.0f}};
    return view;
}

// Reference draw order of the CPU path, as instance handles: frustum test, blended draws
// removed, then a stable sort by (pipeline, material, mesh, submesh) - SceneRenderer::prepare.
std::vector<InstanceHandle> cpuOrder(Fixture& f, const std::vector<MeshDraw>& draws,
                                     const std::vector<InstanceHandle>& handles, const SceneView& view,
                                     VkExtent2D extent) {
    const float aspect = float(extent.width) / float(extent.height);
    const Frustum frustum(view.camera.projectionMatrix(aspect) * view.camera.viewMatrix());
    struct Entry {
        const GraphicsPipeline* pipeline;
        const MeshDraw* draw;
        InstanceHandle handle;
    };
    std::vector<Entry> entries;
    for (size_t i = 0; i < draws.size(); ++i) {
        const MeshDraw& d = draws[i];
        const Bounds world = d.mesh->submeshes()[d.submesh].bounds.transformed(d.transform);
        if (!world.isEmpty() && !frustum.intersects(world)) continue;
        if (f.scene.materials().material(d.material).alphaMode == AlphaMode::Blend) continue;
        entries.push_back({&f.scene.materials().pipeline(d.material), &d, handles[i]});
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.pipeline != b.pipeline) return a.pipeline < b.pipeline;
        if (a.draw->material != b.draw->material) return a.draw->material < b.draw->material;
        if (a.draw->mesh != b.draw->mesh) return a.draw->mesh < b.draw->mesh;
        return a.draw->submesh < b.draw->submesh;
    });
    std::vector<InstanceHandle> order;
    for (const Entry& e : entries) order.push_back(e.handle);
    return order;
}

} // namespace

TEST_CASE("gpu scene: ordered cull - visible list matches the CPU draw order exactly") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    std::vector<MeshDraw> draws = buildScene(f);
    // Many more instances than one 256-thread workgroup, in interleaved groups, with
    // roughly half outside the view, so the block-offset scan and scatter are exercised.
    const MaterialHandle grey = f.material({0.5f, 0.5f, 0.5f, 1}, false);
    for (int i = 0; i < 3000; ++i) {
        const float x = float(i % 60) * 0.9f - 27.0f, z = -float(i / 60) * 0.9f;
        draws.push_back({&f.meshes[size_t(i % 3 == 0 ? 3 : 0)], 0, i % 4 == 0 ? grey : draws[size_t(i % 7)].material,
                         glm::scale(glm::translate(glm::mat4(1), {x, 0.2f, z}), glm::vec3(0.3f))});
    }
    GpuScene gpuScene(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    std::vector<InstanceHandle> handles;
    for (const MeshDraw& d : draws) handles.push_back(gpuScene.add(*d.mesh, d.submesh, d.material, d.transform));
    // Free-list reuse: the re-added instance gets an old slot but is last in submission order.
    gpuScene.remove(handles[5]);
    draws.erase(draws.begin() + 5);
    handles.erase(handles.begin() + 5);
    draws.push_back({&f.meshes[0], 0, grey, glm::translate(glm::mat4(1), {0.5f, 0.5f, -2.0f})});
    handles.push_back(gpuScene.add(*draws.back().mesh, 0, grey, draws.back().transform));

    const VkExtent2D extent{160, 96};
    const SceneView views[] = {cameraAt({0, 4, 6}, {0, 0, -5}), cameraAt({-7, 2, 2}, {2, 0, -6}),
                               cameraAt({3, 1.2f, -2}, {-4, 0.5f, -10}), cameraAt({0, 30, 5}, {0, 0, -25})};
    for (const SceneView& base : views) {
        const std::vector<InstanceHandle> expected = cpuOrder(f, draws, handles, base, extent);
        REQUIRE(!expected.empty());
        SceneView gpuView = base;
        gpuView.gpuScene = &gpuScene;
        const auto gpuImage = f.render(gpuView, extent);
        const std::vector<InstanceHandle> visible = gpuScene.readVisibleList(*gpu().upload);
        CHECK(visible == expected);
        // Deterministic: the same frame renders the same list and image again.
        CHECK(f.render(gpuView, extent) == gpuImage);
        CHECK(gpuScene.readVisibleList(*gpu().upload) == visible);

        SceneView cpuView = base;
        cpuView.draws = draws;
        CHECK(differentBytes(f.render(cpuView, extent), gpuImage) == 0);
    }
}

TEST_CASE("gpu scene: pipeline statistics report scene-pass fragment invocations") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    GpuProfiler profiler(*gpu().context, 2);
    if (!profiler.statisticsEnabled()) {
        MESSAGE("pipelineStatisticsQuery unsupported: statistics are disabled (fallback path)");
        CHECK(profiler.lastStatistics().empty());
        return;
    }
    const std::vector<MeshDraw> draws = buildScene(f);
    GpuScene gpuScene(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    for (const MeshDraw& d : draws) gpuScene.add(*d.mesh, d.submesh, d.material, d.transform);
    const SceneView base = cameraAt({0, 4, 6}, {0, 0, -5});
    const VkExtent2D extent{160, 96};
    // Results are read when a slot is reused: render each path 3 times.
    const auto measure = [&](const SceneView& view) {
        for (int i = 0; i < 3; ++i) f.render(view, extent, nullptr, &profiler);
        const auto& stats = profiler.lastStatistics();
        REQUIRE(stats.size() == 1);
        CHECK(stats[0].name == "Scene");
        return stats[0];
    };
    SceneView cpuView = base;
    cpuView.draws = draws;
    SceneView gpuView = base;
    gpuView.gpuScene = &gpuScene;
    const GpuProfiler::Statistics cpu = measure(cpuView);
    const GpuProfiler::Statistics gpuStats = measure(gpuView);
    MESSAGE("scene fragment invocations: CPU path " << cpu.fragmentInvocations << ", GPU path "
                                                    << gpuStats.fragmentInvocations);
    CHECK(cpu.fragmentInvocations > 0);
    CHECK(cpu.fragmentInvocations >= uint64_t(extent.width) * extent.height / 2); // The ground covers most pixels.
    // Same draws in the same order: the same primitives reach the rasterizer.
    CHECK(gpuStats.inputPrimitives == cpu.inputPrimitives);
    CHECK(gpuStats.clippingPrimitives == cpu.clippingPrimitives);
}

TEST_CASE("gpu scene: GPU-driven output is pixel-identical to the CPU path") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    const std::vector<MeshDraw> draws = buildScene(f);
    GpuScene gpuScene(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    for (const MeshDraw& d : draws) {
        gpuScene.add(*d.mesh, d.submesh, d.material, d.transform);
    }
    CHECK(gpuScene.instanceCount() == draws.size());

    const VkExtent2D extent{160, 96};
    const SceneView views[] = {cameraAt({0, 4, 6}, {0, 0, -5}), cameraAt({-7, 2, 2}, {2, 0, -6}),
                               cameraAt({3, 1.2f, -2}, {-4, 0.5f, -10})}; // Last one cuts through the grid.
    for (const SceneView& base : views) {
        SceneView cpuView = base;
        cpuView.draws = draws;
        RenderStats cpuStats;
        const auto cpuImage = f.render(cpuView, extent, &cpuStats);

        SceneView gpuView = base;
        gpuView.gpuScene = &gpuScene;
        RenderStats gpuStats;
        auto gpuImage = f.render(gpuView, extent, &gpuStats);
        CHECK(differentBytes(cpuImage, gpuImage) == 0);
        CHECK(gpuStats.commandDraws < cpuStats.commandDraws); // Few indirect calls replace many draws.

        // GPU statistics arrive framesInFlight frames later: render the same view again.
        for (int i = 0; i < 2; ++i) gpuImage = f.render(gpuView, extent, &gpuStats);
        CHECK(differentBytes(cpuImage, gpuImage) == 0);
        CHECK(gpuStats.statsLatencyFrames == 2);
        CHECK(gpuStats.instances == cpuStats.instances);
        CHECK(gpuStats.triangles == cpuStats.triangles);
        CHECK(gpuStats.drawCalls == cpuStats.drawCalls);
        CHECK(gpuStats.culled == cpuStats.culled);
    }
}

TEST_CASE("gpu scene: transform, material, removal and add updates stay identical") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    std::vector<MeshDraw> draws = buildScene(f);
    GpuScene gpuScene(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    std::vector<InstanceHandle> handles;
    for (const MeshDraw& d : draws) handles.push_back(gpuScene.add(*d.mesh, d.submesh, d.material, d.transform));

    const SceneView base = cameraAt({0, 4, 6}, {0, 0, -5});
    SceneView gpuView = base;
    gpuView.gpuScene = &gpuScene;
    f.render(gpuView, {128, 96});

    // Move one object, recolour another, remove a third and add a new one.
    draws[3].transform = glm::translate(glm::mat4(1), {0, 2.5f, 0});
    gpuScene.setTransform(handles[3], draws[3].transform);
    const MaterialHandle green = f.material({0.1f, 1.0f, 0.1f, 1}, true);
    draws[10].material = green;
    gpuScene.setMaterial(handles[10], green);
    gpuScene.remove(handles[20]);
    draws.erase(draws.begin() + 20);
    const MeshDraw added{&f.meshes[3], 0, green, glm::translate(glm::mat4(1), {1.0f, 2.0f, 1.0f})};
    draws.push_back(added);
    gpuScene.add(*added.mesh, added.submesh, added.material, added.transform);

    SceneView cpuView = base;
    cpuView.draws = draws;
    const auto cpuImage = f.render(cpuView, {128, 96});
    const auto gpuImage = f.render(gpuView, {128, 96});
    CHECK(differentBytes(cpuImage, gpuImage) == 0);
}

TEST_CASE("gpu scene: buffer and geometry pool growth") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    // Small pool forces geometry growth while meshes are uploaded in one batch.
    GeometryPool pool(*gpu().context, 1024, 1024);
    std::vector<GpuMesh> meshes;
    {
        UploadBatch batch(*gpu().upload);
        for (int i = 0; i < 12; ++i) meshes.emplace_back(batch, primitives::uvSphere(0.3f, 10 + i, 8), &pool);
        batch.submit();
    }
    CHECK(pool.vertexCapacity() > 1024);
    CHECK(meshes[11].baseVertex() > meshes[0].baseVertex());

    // Instances in the renderer's own pool: 3000 > initial capacity (1024).
    const MaterialHandle m = f.material({0.7f, 0.7f, 0.7f, 1}, true);
    GpuScene gpuScene(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    std::vector<MeshDraw> draws;
    for (int i = 0; i < 3000; ++i) {
        const glm::mat4 t = glm::translate(glm::mat4(1), {(i % 60) * 0.2f - 6.0f, (i / 60) * 0.2f - 5.0f, -12.0f});
        draws.push_back({&f.meshes[static_cast<size_t>(i % 4 == 0 ? 3 : 1)], 0, m, glm::scale(t, glm::vec3(0.15f))});
        gpuScene.add(*draws.back().mesh, 0, m, draws.back().transform);
    }
    SceneView base = cameraAt({0, 0, 2}, {0, 0, -12});
    SceneView cpuView = base;
    cpuView.draws = draws;
    SceneView gpuView = base;
    gpuView.gpuScene = &gpuScene;
    const auto cpuImage = f.render(cpuView, {128, 128});
    const auto gpuImage = f.render(gpuView, {128, 128});
    CHECK(differentBytes(cpuImage, gpuImage) == 0);
    CHECK(gpuScene.gpuBytes() > 3000 * (144 + 48));

    // A pooled mesh drawn through the CPU path (the pool is used by both paths).
    GpuScene empty(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    SceneView none = base;
    none.gpuScene = &empty;
    const auto emptyImage = f.render(none, {128, 128});
    CHECK(emptyImage.size() == gpuImage.size());
}

TEST_CASE("gpu scene: meshes outside a geometry pool are rejected") {
    Fixture f;
    UploadBatch batch(*gpu().upload);
    GpuMesh own(batch, primitives::cube(1.0f));
    batch.submit();
    GpuScene gpuScene(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    CHECK_THROWS(gpuScene.add(own, 0, 0, glm::mat4(1)));
    CHECK_THROWS(gpuScene.add(f.meshes[0], 5, 0, glm::mat4(1)));
}
