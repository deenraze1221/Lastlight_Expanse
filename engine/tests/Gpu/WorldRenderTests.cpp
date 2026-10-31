// World -> renderer bridge on a real device: entity-driven rendering must match the existing
// hand-built MeshDraw path pixel for pixel, through both the CPU path and GpuScene.
#include "GpuTestContext.h"

#include "Assets/Primitives.h"
#include "Renderer/GpuScene.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/SceneRenderer.h"
#include "SceneFile/SceneFile.h"
#include "WorldRender/WorldRender.h"

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
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
        meshes.reserve(2);
        meshes.emplace_back(batch, primitives::cube(1.0f), &scene.geometry());
        meshes.emplace_back(batch, primitives::uvSphere(0.6f, 24, 12), &scene.geometry());
        batch.submit();
    }

    MaterialHandle material(glm::vec4 color) {
        MaterialDesc d;
        d.shader = scene.materials().pbrShader();
        d.baseColorFactor = color;
        d.roughness = 0.35f;
        return scene.materials().createMaterial(d);
    }

    std::vector<uint8_t> render(const SceneView& view, VkExtent2D extent) {
        GpuImage out(*gpu().context, {extent.width, extent.height, VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "Out"});
        scratch.beginFrame(static_cast<uint32_t>(frame % 2));
        gpu().upload->immediate([&](VkCommandBuffer cmd) {
            const FrameInfo info{cmd, static_cast<uint32_t>(frame % 2), frame, &scratch, nullptr};
            RenderGraph graph(graphResources);
            RGImportedImage imported;
            imported.image = out.handle();
            imported.view = out.view();
            imported.format = out.format();
            imported.extent = extent;
            imported.final = AccessState{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                                         VK_IMAGE_LAYOUT_GENERAL};
            const RGImage output = graph.importImage("Output", imported);
            scene.addToGraph(graph, info, extent, view, output, out.format(), nullptr);
            graph.execute(info);
        });
        ++frame;
        return gpu().upload->readImage(out, VK_IMAGE_LAYOUT_GENERAL);
    }
};

SceneView baseView() {
    SceneView view;
    view.camera.position = {0, 4, 6};
    view.camera.lookAt({0, 0, -3});
    view.clearColor = {0.05f, 0.06f, 0.08f, 1};
    view.lighting.pointLights = {{{0, 3, 2}, {1.0f, 0.85f, 0.7f}, 25.0f, 15.0f}};
    return view;
}

} // namespace

TEST_CASE("world render: entity rendering matches hand-built draws (CPU path and GpuScene)") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    const MaterialHandle red = f.material({0.8f, 0.2f, 0.2f, 1});
    const MaterialHandle blue = f.material({0.2f, 0.4f, 0.9f, 1});
    const VkExtent2D extent{128, 96};

    // A rotated parent with children: their world matrices come from the hierarchy.
    World world;
    const Entity root = world.create();
    Transform rootTransform;
    rootTransform.position = {0, 0.5f, -3};
    rootTransform.rotation = glm::angleAxis(0.4f, glm::vec3(0, 1, 0));
    world.add<Transform>(root, rootTransform);
    world.add<MeshRenderer>(root, MeshRenderer{&f.meshes[0], 0, red});
    std::vector<Entity> children;
    for (int i = 0; i < 4; ++i) {
        const Entity e = world.create();
        Transform t;
        t.position = {i * 1.5f - 2.25f, 1.2f, 0};
        world.add<Transform>(e, t);
        world.add<MeshRenderer>(e, MeshRenderer{&f.meshes[1], 0, i % 2 ? blue : red});
        world.setParent(e, root);
        children.push_back(e);
    }

    // Reference: the same scene as hand-built MeshDraws (existing rendering path).
    const glm::mat4 rootMatrix = glm::translate(glm::mat4(1), rootTransform.position) *
                                 glm::mat4_cast(rootTransform.rotation);
    std::vector<MeshDraw> manual{{&f.meshes[0], 0, red, rootMatrix}};
    for (int i = 0; i < 4; ++i) {
        manual.push_back({&f.meshes[1], 0, i % 2 ? blue : red,
                          rootMatrix * glm::translate(glm::mat4(1), {i * 1.5f - 2.25f, 1.2f, 0})});
    }
    SceneView manualView = baseView();
    manualView.draws = manual;
    const auto reference = f.render(manualView, extent);

    std::vector<MeshDraw> draws;
    collectDraws(world, draws);
    SceneView cpuView = baseView();
    cpuView.draws = draws;
    CHECK(f.render(cpuView, extent) == reference);

    GpuScene gpuScene(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    GpuSceneSink sink(gpuScene);
    WorldRenderSync sync(sink);
    CHECK(sync.sync(world).added == 5);
    CHECK(gpuScene.instanceCount() == 5);
    SceneView gpuView = baseView();
    gpuView.gpuScene = &gpuScene;
    CHECK(f.render(gpuView, extent) == reference);

    // Updates and destruction: move the root, recolour one child, destroy another.
    world.get<Transform>(root).position.x = 0.7f;
    world.get<MeshRenderer>(children[0]).material = blue;
    world.destroy(children[3]);
    const WorldRenderSync::Stats s = sync.sync(world);
    CHECK(s.updated == 4);
    CHECK(s.removed == 1);
    CHECK(gpuScene.instanceCount() == 4);

    draws.clear();
    collectDraws(world, draws);
    REQUIRE(draws.size() == 4);
    cpuView.draws = draws;
    const auto cpuImage = f.render(cpuView, extent);
    CHECK(cpuImage != reference);
    CHECK(f.render(gpuView, extent) == cpuImage);

    sync.clear();
    CHECK(gpuScene.instanceCount() == 0);
}

TEST_CASE("scene file: JSON scene renders identically to the equivalent C++ scene") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    const VkExtent2D extent{128, 96};

    // C++ reference: same values as examples/scene_from_json, hand-built MeshDraws.
    const MaterialHandle red = f.material({0.8f, 0.2f, 0.2f, 1});
    const MaterialHandle blue = f.material({0.2f, 0.4f, 0.9f, 1});
    const glm::mat4 rootMatrix =
        glm::translate(glm::mat4(1), {0, 0.5f, -3}) * glm::mat4_cast(glm::quat(0.98006658f, 0, 0.19866933f, 0));
    std::vector<MeshDraw> manual{{&f.meshes[0], 0, red, rootMatrix}};
    for (int i = 0; i < 4; ++i) {
        manual.push_back({&f.meshes[1], 0, i % 2 ? blue : red,
                          rootMatrix * glm::translate(glm::mat4(1), {i * 1.5f - 2.25f, 1.2f, 0})});
    }
    SceneView manualView = baseView();
    manualView.draws = manual;
    const auto reference = f.render(manualView, extent);

    // JSON: load, create its materials, bind code-provided meshes, instantiate.
    const SceneDocument doc =
        loadSceneFile(std::filesystem::path(ZODAR_EXAMPLES_DIR) / "scene_from_json" / "scene.scene.json");
    SceneBindings bindings;
    bindings.meshes = {{"cube", &f.meshes[0]}, {"sphere", &f.meshes[1]}};
    bindings.materials = createSceneMaterials(doc, f.scene.materials());
    REQUIRE(bindings.materials.size() == 2);
    World world;
    const SceneInstance instance = instantiateScene(doc, world, bindings);
    CHECK(world.entityCount() == 5);
    CHECK(world.children(instance.byId.at("root")).size() == 4);

    std::vector<MeshDraw> draws;
    collectDraws(world, draws);
    SceneView cpuView = baseView();
    cpuView.draws = draws;
    CHECK(f.render(cpuView, extent) == reference);

    GpuScene gpuScene(*gpu().context, f.scene.materials(), zodar::test::shaderDir(), 2);
    GpuSceneSink sink(gpuScene);
    WorldRenderSync sync(sink);
    CHECK(sync.sync(world).added == 5);
    SceneView gpuView = baseView();
    gpuView.gpuScene = &gpuScene;
    CHECK(f.render(gpuView, extent) == reference);
    sync.clear();

    // GPU-side validation: unknown shader (nothing created), out-of-range submesh.
    SceneDocument badShader = doc;
    badShader.materials[1].data.shader = "game/missing";
    const uint32_t before = f.scene.materials().materialCount();
    CHECK_THROWS_AS((void)createSceneMaterials(badShader, f.scene.materials()), DataError);
    CHECK(f.scene.materials().materialCount() == before);
    CHECK_THROWS_WITH_AS((void)createMaterial(f.scene.materials(), badShader.materials[1].data, "blue"),
                         doctest::Contains("unknown shader 'game/missing'"), DataError);

    SceneDocument badSubmesh = doc;
    badSubmesh.entities[0].meshRenderer->submesh = 9;
    World other;
    CHECK_THROWS_WITH_AS((void)instantiateScene(badSubmesh, other, bindings),
                         doctest::Contains("submesh 9 out of range"), DataError);
    CHECK(other.entityCount() == 0);
}
