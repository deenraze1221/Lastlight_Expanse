#include "GpuTestContext.h"

#include "Assets/Primitives.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/SceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <vector>

using namespace raven;
using namespace raven::gfx;
using raven::test::gpu;

namespace {

constexpr uint32_t kSize = 64;

struct Fixture {
    SceneRenderer scene{*gpu().context, *gpu().upload, {raven::test::shaderDir()}};
    FrameAllocator scratch{*gpu().context, 2};
    GpuMesh cube;
    GpuMesh sphere;
    GpuMesh plane;
    uint64_t frame = 0;

    Fixture() {
        UploadBatch batch(*gpu().upload);
        cube = GpuMesh(batch, primitives::cube(1.0f));
        sphere = GpuMesh(batch, primitives::uvSphere(1.0f, 48, 24));
        plane = GpuMesh(batch, primitives::plane(1.0f, 1.0f));
        batch.submit();
    }

    MaterialHandle unlit(glm::vec4 color, AlphaMode mode = AlphaMode::Opaque) {
        MaterialDesc d;
        d.shader = scene.materials().unlitShader();
        d.baseColorFactor = color;
        d.alphaMode = mode;
        return scene.materials().createMaterial(d);
    }

    RenderStats render(const SceneView& view, VkExtent2D extent = {kSize, kSize}) {
        RenderStats stats;
        // Each immediate() completes before returning, so slots can be recycled right away.
        scratch.beginFrame(static_cast<uint32_t>(frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            stats = scene.render({cmd, static_cast<uint32_t>(frame % 2), frame, &scratch, nullptr}, extent, view);
        });
        ++frame;
        return stats;
    }

    // Scene colour as floats (RGBA16F target).
    std::vector<glm::vec4> readColor() {
        const auto bytes = gpu().upload->readImage(scene.colorTarget(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        std::vector<glm::vec4> out(bytes.size() / 8);
        const auto* halves = reinterpret_cast<const uint16_t*>(bytes.data());
        for (size_t i = 0; i < out.size(); ++i) {
            out[i] = {glm::unpackHalf1x16(halves[i * 4]), glm::unpackHalf1x16(halves[i * 4 + 1]),
                      glm::unpackHalf1x16(halves[i * 4 + 2]), glm::unpackHalf1x16(halves[i * 4 + 3])};
        }
        return out;
    }
};

SceneView frontCamera() {
    SceneView view;
    view.camera.position = {0, 0, 5};
    view.camera.lookAt({0, 0, 0});
    view.clearColor = {0, 0, 0, 1};
    return view;
}

glm::vec4 at(const std::vector<glm::vec4>& img, uint32_t x, uint32_t y, uint32_t width = kSize) {
    return img[static_cast<size_t>(y) * width + x];
}

} // namespace

TEST_CASE("scene: depth test keeps the nearest surface regardless of draw order") {
    raven::test::ValidationGuard guard;
    Fixture f;
    const MaterialHandle red = f.unlit({1, 0, 0, 1});
    const MaterialHandle green = f.unlit({0, 1, 0, 1});
    const glm::mat4 nearT = glm::translate(glm::mat4(1), {0, 0, 1});   // Closer to the camera.
    const glm::mat4 farT = glm::scale(glm::translate(glm::mat4(1), {0, 0, -1}), glm::vec3(2));

    for (int order = 0; order < 2; ++order) {
        std::vector<MeshDraw> draws = {{&f.cube, 0, red, nearT}, {&f.cube, 0, green, farT}};
        if (order == 1) std::swap(draws[0], draws[1]);
        SceneView view = frontCamera();
        view.draws = draws;
        f.render(view);
        const auto img = f.readColor();
        CHECK(at(img, 32, 32).r == doctest::Approx(1.0f));
        CHECK(at(img, 32, 32).g == doctest::Approx(0.0f));
        CHECK(at(img, 32, 22).g == doctest::Approx(1.0f)); // Far cube (rows 21-43) visible above the near one (24-40).
        CHECK(at(img, 0, 0).r == doctest::Approx(0.0f));  // Background.
    }
}

TEST_CASE("scene: reverse-Z depth values match the projection") {
    raven::test::ValidationGuard guard;
    Fixture f;
    std::vector<MeshDraw> draws = {{&f.plane, 0, f.unlit({1, 1, 1, 1}),
                                    glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(1, 0, 0))}};
    SceneView view = frontCamera(); // Plane faces +Z at distance 5.
    view.draws = draws;
    f.render(view);
    const auto bytes = gpu().upload->readImage(f.scene.depthTarget(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    const auto* depth = reinterpret_cast<const float*>(bytes.data());
    CHECK(depth[32 * kSize + 32] == doctest::Approx(view.camera.projection.nearPlane / 5.0f).epsilon(0.001));
    CHECK(depth[0] == 0.0f); // Cleared to the far value.
}

TEST_CASE("scene: PBR lighting responds to sun direction and point lights") {
    raven::test::ValidationGuard guard;
    Fixture f;
    MaterialDesc d;
    d.shader = f.scene.materials().pbrShader();
    d.baseColorFactor = {0.8f, 0.8f, 0.8f, 1};
    d.roughness = 0.6f;
    const MaterialHandle grey = f.scene.materials().createMaterial(d);
    std::vector<MeshDraw> draws = {{&f.sphere, 0, grey, glm::mat4(1)}};

    SceneView view = frontCamera();
    view.draws = draws;
    view.lighting.ambientIntensity = 0.0f;
    view.lighting.sun.direction = {0, -1, 0}; // From above.
    f.render(view);
    auto img = f.readColor();
    const float top = at(img, 32, 22).r;
    const float bottom = at(img, 32, 42).r;
    CHECK(top > 0.2f);
    CHECK(bottom == doctest::Approx(0.0f)); // Facing away, no ambient.

    view.lighting.sun.enabled = false;
    view.lighting.pointLights = {{{0, 0, 3}, {1, 1, 1}, 20.0f, 0.0f}};
    f.render(view);
    img = f.readColor();
    const float centre = at(img, 32, 32).r;
    const float edge = at(img, 32, 21).r;
    CHECK(centre > edge); // Closest, most facing point is brightest.
    CHECK(centre > 0.5f);
}

TEST_CASE("scene: textured unlit material and alpha modes") {
    raven::test::ValidationGuard guard;
    Fixture f;
    UploadBatch batch(*gpu().upload);
    ImageData checker;
    checker.width = checker.height = 2;
    checker.colorSpace = ColorSpace::Linear;
    // Left column opaque blue, right column transparent.
    checker.levels.push_back({0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0});
    Texture texture(batch, f.scene.bindless(), checker);
    batch.submit();

    MaterialDesc masked;
    masked.shader = f.scene.materials().unlitShader();
    masked.alphaMode = AlphaMode::Mask;
    masked.baseColor = {&texture, SamplerDesc::nearest()};
    const MaterialHandle mask = f.scene.materials().createMaterial(masked);
    const MaterialHandle glass = f.unlit({1, 0, 0, 0.5f}, AlphaMode::Blend);
    const MaterialHandle backdrop = f.unlit({0, 1, 0, 1});

    const glm::mat4 faceCamera = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(1, 0, 0));
    std::vector<MeshDraw> draws = {
        {&f.plane, 0, glass, glm::translate(glm::mat4(1), {0, 0, 1}) * glm::scale(faceCamera, glm::vec3(0.6f))},
        {&f.plane, 0, mask, glm::scale(faceCamera, glm::vec3(5.0f))}, // Covers pixels ~4..60.
        {&f.plane, 0, backdrop, glm::translate(glm::mat4(1), {0, 0, -1}) * glm::scale(faceCamera, glm::vec3(10.0f))},
    };
    SceneView view = frontCamera();
    view.draws = draws;
    f.render(view);
    const auto img = f.readColor();
    CHECK(at(img, 12, 20).b == doctest::Approx(1.0f)); // Opaque texel of the masked plane.
    CHECK(at(img, 52, 20).g == doctest::Approx(1.0f)); // Discarded texel shows the backdrop.
    CHECK(at(img, 52, 20).b == doctest::Approx(0.0f));
    const glm::vec4 blended = at(img, 33, 32);         // Glass over the blue/green boundary region.
    CHECK(blended.r == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("scene: culling, instancing and statistics") {
    raven::test::ValidationGuard guard;
    Fixture f;
    const MaterialHandle white = f.unlit({1, 1, 1, 1});
    std::vector<MeshDraw> draws;
    for (int i = 0; i < 100; ++i) {
        draws.push_back({&f.cube, 0, white, glm::translate(glm::mat4(1), {(i % 10) * 0.1f - 0.5f, (i / 10) * 0.1f - 0.5f, 0})});
    }
    draws.push_back({&f.cube, 0, white, glm::translate(glm::mat4(1), {0, 0, 20})}); // Behind the camera.
    SceneView view = frontCamera();
    view.draws = draws;
    const RenderStats stats = f.render(view);
    CHECK(stats.submitted == 101);
    CHECK(stats.culled == 1);
    CHECK(stats.instances == 100);
    CHECK(stats.drawCalls == 1);

    // More instances than the initial capacity forces a buffer reallocation.
    std::vector<MeshDraw> many(3000, MeshDraw{&f.cube, 0, white, glm::mat4(1)});
    view.draws = many;
    CHECK(f.render(view).instances == 3000);
}

TEST_CASE("scene: target resize retires old targets safely and resolves to an output") {
    raven::test::ValidationGuard guard;
    Fixture f;
    std::vector<MeshDraw> draws = {{&f.cube, 0, f.unlit({0.25f, 0.5f, 2.0f, 1}), glm::mat4(1)}};
    SceneView view = frontCamera();
    view.draws = draws;
    const uint32_t baseTextures = f.scene.bindless().textureCount();
    f.render(view, {64, 64});
    f.render(view, {32, 16});
    f.render(view, {48, 48});
    for (int i = 0; i < 4; ++i) {
        f.render(view, {48, 48});
    }
    CHECK(f.scene.bindless().textureCount() == baseTextures + 1); // Old target slots were released.

    GpuImage output(*gpu().context, {48, 48, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "Output"});
    gpu().upload->immediate([&](VkCommandBuffer cmd) {
        transitionImage(cmd, output.handle(), output.fullRange(), VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        f.scene.resolve({cmd, 0, f.frame, &f.scratch, nullptr}, output.view(), VK_FORMAT_R8G8B8A8_UNORM, {48, 48});
        transitionImage(cmd, output.handle(), output.fullRange(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_READ_BIT);
    });
    const auto pixels = gpu().upload->readImage(output, VK_IMAGE_LAYOUT_GENERAL);
    const uint8_t* centre = &pixels[(24 * 48 + 24) * 4];
    // UNORM display output is sRGB-encoded by the resolve shader.
    CHECK(std::abs(int(centre[0]) - 137) <= 1); // linear 0.25
    CHECK(std::abs(int(centre[1]) - 188) <= 1); // linear 0.5
    CHECK(centre[2] == 255);                    // 2.0 clamped (no tone mapping yet)
}
