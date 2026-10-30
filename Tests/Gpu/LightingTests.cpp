#include "GpuTestContext.h"

#include "Assets/Primitives.h"
#include "Renderer/GpuScene.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/SceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <cmath>
#include <vector>

using namespace zodar;
using namespace zodar::gfx;
using zodar::test::gpu;

namespace {

float hash01(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return float(h & 0xFFFFFF) / float(0x1000000);
}

struct Fixture {
    SceneRenderer scene{*gpu().context, *gpu().upload, {zodar::test::shaderDir()}};
    RenderGraphResources graphResources{*gpu().context, scene.bindless()};
    FrameAllocator scratch{*gpu().context, 2};
    std::vector<GpuMesh> meshes;
    std::vector<MeshDraw> draws;
    uint64_t frame = 0;

    Fixture() {
        UploadBatch batch(*gpu().upload);
        meshes.reserve(3);
        meshes.emplace_back(batch, primitives::plane(1.0f, 1.0f), &scene.geometry());
        meshes.emplace_back(batch, primitives::uvSphere(0.5f, 24, 12), &scene.geometry());
        meshes.emplace_back(batch, primitives::cube(0.8f), &scene.geometry());
        batch.submit();
        const MaterialHandle ground = pbr({0.7f, 0.7f, 0.7f, 1}, 0.0f, 0.6f);
        const MaterialHandle metal = pbr({0.9f, 0.6f, 0.3f, 1}, 1.0f, 0.3f);
        const MaterialHandle plastic = pbr({0.2f, 0.4f, 0.8f, 1}, 0.0f, 0.4f);
        draws.push_back({&meshes[0], 0, ground, glm::scale(glm::mat4(1), glm::vec3(40.0f))});
        for (int i = 0; i < 25; ++i) {
            const glm::vec3 p((i % 5) * 3.0f - 6.0f, 0.5f, (i / 5) * -3.0f + 2.0f);
            draws.push_back({&meshes[size_t(1 + i % 2)], 0, i % 3 == 0 ? metal : plastic, glm::translate(glm::mat4(1), p)});
        }
    }

    MaterialHandle pbr(glm::vec4 color, float metallic, float roughness) {
        MaterialDesc d;
        d.shader = scene.materials().pbrShader();
        d.baseColorFactor = color;
        d.metallic = metallic;
        d.roughness = roughness;
        return scene.materials().createMaterial(d);
    }

    // Reference path; returns the raw HDR (RGBA16F) scene colour.
    std::vector<uint8_t> renderReference(const SceneView& view, VkExtent2D extent, RenderStats* stats = nullptr) {
        scratch.beginFrame(uint32_t(frame % 2));
        RenderStats s;
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            s = scene.render({cmd, uint32_t(frame % 2), frame, &scratch, nullptr}, extent, view);
        });
        ++frame;
        if (stats) *stats = s;
        return gpu().upload->readImage(scene.colorTarget(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Render-graph path, resolved to RGBA8.
    std::vector<uint8_t> renderGraph(const SceneView& view, VkExtent2D extent, RenderStats* stats = nullptr) {
        GpuImage out(*gpu().context, {extent.width, extent.height, VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "Out"});
        scratch.beginFrame(uint32_t(frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            const FrameInfo info{cmd, uint32_t(frame % 2), frame, &scratch, nullptr};
            RenderGraph graph(graphResources);
            RGImportedImage imported;
            imported.image = out.handle();
            imported.view = out.view();
            imported.format = out.format();
            imported.extent = extent;
            imported.final = AccessState{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                                         VK_IMAGE_LAYOUT_GENERAL};
            scene.addToGraph(graph, info, extent, view, graph.importImage("Output", imported), out.format(), stats);
            graph.execute(info);
        });
        ++frame;
        return gpu().upload->readImage(out, VK_IMAGE_LAYOUT_GENERAL);
    }
};

SceneView baseView() {
    SceneView view;
    view.camera.position = {0.0f, 4.0f, 9.0f};
    view.camera.lookAt({0.0f, 0.0f, -2.0f});
    view.clearColor = {0.02f, 0.02f, 0.03f, 1};
    view.lighting.ambientIntensity = 0.02f;
    view.lighting.sun.enabled = false;
    return view;
}

// Many coloured point and spot lights of mixed ranges (some automatic) over the scene,
// including lights around the camera and lights crossing cluster borders.
void addLights(SceneLighting& lighting, uint32_t points, uint32_t spots, uint32_t seed = 1) {
    for (uint32_t i = 0; i < points; ++i) {
        PointLight p;
        p.position = {hash01(i, seed) * 24.0f - 12.0f, 0.2f + hash01(i, seed + 1) * 3.0f, hash01(i, seed + 2) * -20.0f + 8.0f};
        p.color = {0.3f + hash01(i, seed + 3), 0.3f + hash01(i, seed + 4), 0.3f + hash01(i, seed + 5)};
        p.intensity = 0.3f + hash01(i, seed + 6) * (i % 16 == 0 ? 0.6f : 3.0f);
        p.range = i % 16 == 0 ? 0.0f : 1.0f + hash01(i, seed + 7) * 4.0f;
        lighting.pointLights.push_back(p);
    }
    for (uint32_t i = 0; i < spots; ++i) {
        SpotLight s;
        s.position = {hash01(i, seed + 8) * 20.0f - 10.0f, 3.0f + hash01(i, seed + 9) * 2.0f, hash01(i, seed + 10) * -16.0f + 4.0f};
        s.direction = glm::normalize(glm::vec3(hash01(i, seed + 11) - 0.5f, -1.0f, hash01(i, seed + 12) - 0.5f));
        s.color = {1.0f, 0.9f, 0.7f};
        s.intensity = 2.0f + hash01(i, seed + 13) * 8.0f;
        s.range = 4.0f + hash01(i, seed + 14) * 6.0f;
        s.innerConeRadians = 0.2f + hash01(i, seed + 15) * 0.3f;
        s.outerConeRadians = s.innerConeRadians + 0.05f + hash01(i, seed + 16) * 0.3f;
        lighting.spotLights.push_back(s);
    }
}

size_t differentBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    REQUIRE(a.size() == b.size());
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

} // namespace

TEST_CASE("clustered lighting: identical to evaluating every light (all qualities, both paths)") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    const VkExtent2D extent{200, 120};
    for (LightingQuality q : {LightingQuality::Low, LightingQuality::Medium, LightingQuality::High, LightingQuality::Ultra}) {
        const std::string quality = toString(q);
        CAPTURE(quality);
        LightingSettings settings;
        settings.quality = q;
        f.scene.setLightingSettings(settings);
        // Dense enough to fill many clusters, sparse enough for each preset's list capacity
        // (at 200x120 a low-quality tile covers half the screen).
        SceneView view = baseView();
        view.draws = f.draws;
        const bool coarse = q == LightingQuality::Low || q == LightingQuality::Medium;
        addLights(view.lighting, coarse ? 90 : 300, coarse ? 20 : 60);
        SceneView brute = view;
        brute.bruteForceLights = true;
        const auto reference = f.renderReference(brute, extent);
        RenderStats stats;
        const auto clustered = f.renderReference(view, extent, &stats);
        f.renderReference(view, extent);
        f.renderReference(view, extent, &stats); // Statistics of the clustered frame
        REQUIRE(stats.lightingStatsValid);
        CHECK(stats.overflowClusters == 0); // Identity holds only without list overflow.
        MESSAGE(quality << ": " << stats.clusters << " clusters, max " << stats.maxLightsInCluster << " lights");
        CHECK(differentBytes(reference, clustered) == 0); // Raw HDR halves.
        CHECK(differentBytes(f.renderGraph(brute, extent), f.renderGraph(view, extent)) == 0);
    }
}

TEST_CASE("clustered lighting: GPU statistics and overflow reporting") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    SceneView view = baseView();
    view.draws = f.draws;
    addLights(view.lighting, 300, 60);
    const VkExtent2D extent{200, 120};
    RenderStats stats;
    for (int i = 0; i < 3; ++i) f.renderGraph(view, extent, &stats); // Stats arrive framesInFlight later.
    CHECK(stats.lights == 360);
    CHECK(stats.lightingStatsValid);
    CHECK(stats.clusters > 0);
    CHECK(stats.clusterAssignments > 0);
    CHECK(stats.maxLightsInCluster > 0);
    CHECK(stats.overflowClusters == 0);
    MESSAGE("360 lights: " << stats.clusters << " clusters, " << stats.clusterAssignments << " assignments, max "
                           << stats.maxLightsInCluster << " per cluster");

    // 200 overlapping lights exceed the low preset's 64-light lists: reported, not silent.
    SceneView crowded = baseView();
    crowded.draws = f.draws;
    for (int i = 0; i < 200; ++i) crowded.lighting.pointLights.push_back({{0.0f, 1.0f, 0.0f}, {1, 1, 1}, 0.01f, 30.0f});
    LightingSettings low;
    low.quality = LightingQuality::Low;
    f.scene.setLightingSettings(low);
    for (int i = 0; i < 3; ++i) f.renderGraph(crowded, extent, &stats);
    CHECK(stats.overflowClusters > 0);
    CHECK(stats.maxLightsInCluster == 200);
}

TEST_CASE("clustered lighting: spot light cone and inverse-square falloff") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    SceneView view;
    view.camera.position = {0.0f, 10.0f, 0.0f};
    view.camera.lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}); // Straight down: up must not be +Y
    view.lighting.ambientIntensity = 0.0f;
    view.lighting.sun.enabled = false;
    std::vector<MeshDraw> draws = {f.draws[0]}; // Ground only
    view.draws = draws;
    SpotLight spot;
    spot.position = {0.0f, 4.0f, 0.0f};
    spot.direction = {0.0f, -1.0f, 0.0f};
    spot.intensity = 20.0f;
    spot.range = 20.0f;
    spot.innerConeRadians = 0.25f;
    spot.outerConeRadians = 0.35f;
    view.lighting.spotLights = {spot};
    const VkExtent2D extent{128, 128};
    const auto hdr = f.renderReference(view, extent);
    const auto red = [&](uint32_t x, uint32_t y) {
        return glm::unpackHalf1x16(reinterpret_cast<const uint16_t*>(hdr.data())[(y * extent.width + x) * 4]);
    };
    // Lit centre; dark outside the cone (radius on the ground: 4 * tan(0.35) = 1.46).
    CHECK(red(64, 64) > 0.1f);
    CHECK(red(64, 2) == 0.0f);
    CHECK(red(2, 64) == 0.0f);

    // Point light: irradiance falls with 1/d^2 (Lambert at normal incidence on the ground).
    view.lighting.spotLights.clear();
    view.lighting.pointLights = {{{0.0f, 1.0f, 0.0f}, {1, 1, 1}, 4.0f, 1000.0f}};
    const auto near = f.renderReference(view, extent);
    view.lighting.pointLights[0].position.y = 2.0f;
    const auto far = f.renderReference(view, extent);
    const auto centre = [&](const std::vector<uint8_t>& img) {
        return glm::unpackHalf1x16(reinterpret_cast<const uint16_t*>(img.data())[(64 * extent.width + 64) * 4]);
    };
    CHECK(centre(near) / centre(far) == doctest::Approx(4.0f).epsilon(0.05)); // (2/1)^2
}

TEST_CASE("clustered lighting: thousands of lights and debug views render without errors") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    SceneView view = baseView();
    view.draws = f.draws;
    addLights(view.lighting, 4000, 1000, 7);
    const VkExtent2D extent{160, 96};
    RenderStats stats;
    const auto lit = f.renderGraph(view, extent, &stats);
    CHECK(stats.lights == 5000);
    view.debugView = LightingDebugView::LightCount;
    const auto heat = f.renderGraph(view, extent);
    CHECK(differentBytes(lit, heat) > 0);
    for (LightingDebugView v : {LightingDebugView::ClusterSlice, LightingDebugView::Normals, LightingDebugView::Direct,
                                LightingDebugView::Indirect, LightingDebugView::SpecularIbl}) {
        view.debugView = v;
        f.renderGraph(view, extent);
    }
}

TEST_CASE("depth prepass: identical image with and without (reference and graph paths)") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    SceneView view = baseView();
    view.draws = f.draws;
    addLights(view.lighting, 100, 20);
    // Masked and two-sided geometry exercises the prepass alpha test and culling variants.
    MaterialDesc masked;
    masked.shader = f.scene.materials().pbrShader();
    masked.alphaMode = AlphaMode::Mask;
    masked.doubleSided = true;
    masked.baseColorFactor = {0.9f, 0.9f, 0.2f, 0.3f}; // Below the 0.5 cutoff: fully discarded
    const MaterialHandle hidden = f.scene.materials().createMaterial(masked);
    std::vector<MeshDraw> draws = f.draws;
    draws.push_back({&f.meshes[0], 0, hidden, glm::translate(glm::mat4(1), {0.0f, 1.0f, 3.0f}) *
                                                  glm::rotate(glm::mat4(1), 1.5708f, glm::vec3(1, 0, 0)) *
                                                  glm::scale(glm::mat4(1), glm::vec3(4.0f))});
    view.draws = draws;
    const VkExtent2D extent{160, 96};
    LightingSettings settings;
    settings.depthPrepass = false;
    f.scene.setLightingSettings(settings);
    const auto without = f.renderReference(view, extent);
    const auto withoutGraph = f.renderGraph(view, extent);
    settings.depthPrepass = true;
    f.scene.setLightingSettings(settings);
    CHECK(differentBytes(without, f.renderReference(view, extent)) == 0);
    CHECK(differentBytes(withoutGraph, f.renderGraph(view, extent)) == 0);
}
