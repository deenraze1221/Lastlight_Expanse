// Phase C on a real device: AssetManager loading, caching, release and pixel-art sampling.
#include "GpuTestContext.h"

#include "AssetManager/AssetManager.h"
#include "Assets/Primitives.h"
#include "Core/ImageIO.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/SceneRenderer.h"
#include "SceneFile/SceneFile.h"

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace zodar;
using namespace zodar::gfx;
using zodar::test::gpu;

namespace {

const std::filesystem::path kExampleDir = std::filesystem::path(ZODAR_EXAMPLES_DIR) / "asset_loading";

// Temporary asset folder: a 32x32 black/white checker (one texel per cell), a 48x48 texture
// and material files using it with different filters.
struct AssetDir {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "zodar_asset_manager_tests";

    AssetDir() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "materials");
        std::vector<uint8_t> checker(32 * 32 * 4);
        for (uint32_t y = 0; y < 32; ++y) {
            for (uint32_t x = 0; x < 32; ++x) {
                const uint8_t v = (x + y) % 2 ? 255 : 0;
                uint8_t* p = &checker[(y * 32 + x) * 4];
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
        }
        REQUIRE(writePng(dir / "checker.png", 32, 32, checker));
        std::vector<uint8_t> odd(48 * 48 * 4, 128);
        REQUIRE(writePng(dir / "odd.png", 48, 48, odd));
        write("materials/pixel.material.json",
              R"({"shader": "zodar/unlit", "baseColorTexture": {"path": "../checker.png", "filter": "pixelArt"}})");
        write("materials/linear.material.json",
              R"({"shader": "zodar/unlit", "baseColorTexture": {"path": "../checker.png", "filter": "linear"}})");
        write("materials/missing.material.json", R"({"baseColorTexture": "../missing.png"})");
        write("materials/shader.material.json", R"({"shader": "game/unknown"})");
    }
    ~AssetDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void write(const std::string& name, const std::string& text) const {
        std::ofstream(dir / name, std::ios::binary) << text;
    }
};

struct Fixture {
    SceneRenderer scene{*gpu().context, *gpu().upload, {zodar::test::shaderDir()}};
    RenderGraphResources graphResources{*gpu().context, scene.bindless()};
    FrameAllocator scratch{*gpu().context, 2};
    uint64_t frame = 0;

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

    // Runs retired destructions: one submission schedules them, the wait completes them.
    static void collect() {
        gpu().upload->immediate([](VkCommandBuffer) {});
        gpu().context->waitIdle();
        gpu().context->collectGarbage();
    }
};

size_t distinctColors(const std::vector<uint8_t>& rgba) {
    std::set<uint32_t> colors;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        colors.insert(uint32_t(rgba[i]) | uint32_t(rgba[i + 1]) << 8 | uint32_t(rgba[i + 2]) << 16);
    }
    return colors.size();
}

// A 2x2 plane facing the camera that fills the whole view (magnified ~5 px per texel).
// `draws` backs the view's draw span.
SceneView planeView(const GpuMesh& plane, MaterialHandle material, std::vector<MeshDraw>& draws) {
    SceneView view;
    view.camera.position = {0, 0, 1};
    view.camera.lookAt({0, 0, 0});
    view.lighting.sun.enabled = false;
    draws = {{&plane, 0, material, glm::rotate(glm::mat4(1), glm::half_pi<float>(), glm::vec3(1, 0, 0))}};
    view.draws = draws;
    return view;
}

} // namespace

TEST_CASE("asset manager: pixel-art materials sample PNG texels with hard edges, linear ones blend") {
    zodar::test::ValidationGuard guard;
    const AssetDir files;
    Fixture f;
    AssetManagerDesc desc;
    desc.root = files.dir;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);
    std::optional<GpuMesh> plane;
    {
        UploadBatch batch(*gpu().upload);
        plane.emplace(batch, primitives::plane(2.0f, 2.0f));
        batch.submit();
    }
    const MaterialAssetHandle pixel = assets.loadMaterial("materials/pixel.material.json");
    const MaterialAssetHandle linear = assets.loadMaterial("materials/linear.material.json");
    REQUIRE(pixel.ready());
    REQUIRE(linear.ready());
    const MaterialDesc& pd = f.scene.materials().material(pixel->handle);
    CHECK(pd.baseColor.sampler == SamplerDesc::pixelArt());
    CHECK(pd.baseColor.texture == &pixel->textures.at(0)->texture);

    const VkExtent2D extent{128, 96};
    std::vector<MeshDraw> draws;
    const auto pixelImage = f.render(planeView(*plane, pixel->handle, draws), extent);
    const auto linearImage = f.render(planeView(*plane, linear->handle, draws), extent);
    CHECK(distinctColors(pixelImage) == 2); // only black and white texels: nearest sampling
    CHECK(distinctColors(linearImage) > 8); // bilinear magnification blends neighbours
    gpu().context->waitIdle();
}

TEST_CASE("asset manager: caching, shared textures and deferred release") {
    zodar::test::ValidationGuard guard;
    const AssetDir files;
    Fixture f;
    AssetManagerDesc desc;
    desc.root = files.dir;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);
    MaterialSystem& materials = f.scene.materials();

    MaterialAssetHandle a = assets.loadMaterial("materials/pixel.material.json");
    MaterialAssetHandle b = assets.loadMaterial(files.dir / "materials" / ".." / "materials" / "pixel.material.json");
    REQUIRE(a.ready());
    CHECK(a == b);
    TextureHandle texture = assets.loadTexture("checker.png");
    REQUIRE(texture.ready());
    CHECK(texture == a->textures.at(0)); // same file + usage: one GPU texture
    CHECK(texture->width == 32);
    CHECK((texture->sizeClass == TextureSizeClass::Small));
    CHECK(texture->mipLevels == 6);
    CHECK(texture->warnings.empty());
    MaterialAssetHandle c = assets.loadMaterial("materials/linear.material.json");
    CHECK(c->textures.at(0) == texture);
    AssetManager::Stats s = assets.stats();
    CHECK(s.textures == 1);
    CHECK(s.materials == 2);
    CHECK(s.hits >= 3);

    // Data-usage loads of the same file are separate textures (linear colour space).
    TextureHandle data = assets.loadTexture("checker.png", {TextureUsage::Data, false});
    CHECK_FALSE(data == texture);
    CHECK(data->mipLevels == 1);
    data = {};

    const MaterialHandle released = a->handle;
    const uint32_t freeBefore = materials.freeMaterialCount();
    a = {};
    b = {};
    CHECK(assets.stats().materials == 1);
    CHECK(materials.freeMaterialCount() == freeBefore); // deferred until the GPU is done
    Fixture::collect();
    CHECK(materials.freeMaterialCount() == freeBefore + 1);
    // The freed slot is reused.
    MaterialAssetHandle again = assets.loadMaterial("materials/pixel.material.json");
    REQUIRE(again.ready());
    CHECK(again->handle == released);
    CHECK(again->textures.at(0) == texture);

    // Textures stay alive while a material uses them.
    texture = {};
    again = {};
    CHECK(assets.stats().textures == 1); // still referenced by `c`
    c = {};
    CHECK(assets.stats().textures == 0);
    Fixture::collect();
    CHECK(assets.stats().released >= 5);
}

TEST_CASE("asset manager: failures carry messages; size warnings do not fail") {
    zodar::test::ValidationGuard guard;
    const AssetDir files;
    Fixture f;
    AssetManagerDesc desc;
    desc.root = files.dir;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);

    const TextureHandle missing = assets.loadTexture("nope.png");
    CHECK(missing.failed());
    CHECK(missing.error().find("nope.png") != std::string::npos);
    const MaterialAssetHandle badTexture = assets.loadMaterial("materials/missing.material.json");
    CHECK(badTexture.failed());
    CHECK(badTexture.error().find("baseColorTexture") != std::string::npos);
    CHECK(badTexture.error().find("missing.png") != std::string::npos);
    const MaterialAssetHandle badShader = assets.loadMaterial("materials/shader.material.json");
    CHECK(badShader.failed());
    CHECK(badShader.error().find("game/unknown") != std::string::npos);
    const MaterialAssetHandle noFile = assets.loadMaterial("materials/none.material.json");
    CHECK(noFile.failed());
    const ModelHandle badModel = assets.loadModel("checker.png");
    CHECK(badModel.failed());
    CHECK(assets.stats().failures >= 5);

    const TextureHandle odd = assets.loadTexture("odd.png");
    REQUIRE(odd.ready());
    CHECK((odd->sizeClass == TextureSizeClass::NonStandard));
    REQUIRE(odd->warnings.size() == 1);
    CHECK(odd->warnings[0].find("48x48") != std::string::npos);

    // Scene materials: every failure listed in one DataError.
    const SceneDocument doc = parseScene(R"({"version": 1, "materials": {
        "a": {"baseColorTexture": "missing_a.png"}, "b": {"baseColorTexture": "checker.png"}}})",
                                         (files.dir / "s.scene.json").generic_string());
    const size_t materialsBefore = assets.stats().materials; // failed entries above are still held
    try {
        (void)assets.createSceneMaterials(doc, files.dir / "s.scene.json");
        FAIL("expected DataError");
    } catch (const DataError& e) {
        REQUIRE(e.issues().size() == 1);
        CHECK(e.issues()[0].path == "/materials/a");
        CHECK(e.issues()[0].message.find("missing_a.png") != std::string::npos);
    }
    CHECK(assets.stats().materials == materialsBefore); // the scene's materials were released again
}

TEST_CASE("asset manager: glTF model and scene materials from the asset_loading example") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    AssetManagerDesc desc;
    desc.root = kExampleDir;
    desc.defaultSampler = SamplerDesc::pixelArt();
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);
    MaterialSystem& materials = f.scene.materials();

    ModelHandle crate = assets.loadModel("crate.gltf");
    REQUIRE_MESSAGE(crate.ready(), (crate.failed() ? crate.error() : ""));
    CHECK(crate->warnings.empty()); // 64x64 texture: standard size
    CHECK(crate->model.meshCount() == 1);
    CHECK(crate->model.textureCount() == 1);
    REQUIRE(crate->model.materials().size() == 1);
    // The file's sampler (NEAREST / NEAREST_MIPMAP_LINEAR, clamp) is honoured end to end.
    const SamplerDesc fileSampler = materials.material(crate->model.materials()[0]).baseColor.sampler;
    CHECK(fileSampler.magFilter == Filter::Nearest);
    CHECK(fileSampler.minFilter == Filter::Nearest);
    CHECK(fileSampler.mipFilter == Filter::Linear);
    CHECK(fileSampler.addressU == AddressMode::ClampToEdge);
    CHECK(assets.loadModel("crate.gltf") == crate);

    ModelLoadSettings linear;
    linear.samplerOverride = SamplerDesc::linear();
    ModelHandle crateLinear = assets.loadModel("crate.gltf", linear);
    REQUIRE(crateLinear.ready());
    CHECK_FALSE(crateLinear == crate);
    CHECK(materials.material(crateLinear->model.materials()[0]).baseColor.sampler.magFilter == Filter::Linear);

    const std::filesystem::path scenePath = kExampleDir / "asset_loading.scene.json";
    const SceneDocument doc = loadSceneFile(scenePath);
    SceneMaterialAssets sceneMaterials = assets.createSceneMaterials(doc, scenePath);
    REQUIRE(sceneMaterials.byId.size() == 2);
    const MaterialAssetHandle& rune = sceneMaterials.byId.at("rune");
    REQUIRE(rune.ready());
    REQUIRE(rune->textures.size() == 2);
    CHECK(rune->textures[0] == rune->textures[1]); // base colour + emissive share rune.png
    CHECK((rune->textures[0]->sizeClass == TextureSizeClass::Small));
    CHECK(materials.material(rune->handle).emissiveTexture.sampler == SamplerDesc::pixelArt());
    CHECK((sceneMaterials.byId.at("crateLinear")->textures.at(0)->sizeClass == TextureSizeClass::Primary));
    CHECK(sceneMaterials.handles().size() == 2);

    // Draw the crate: something other than the clear colour is visible.
    SceneView view;
    view.camera.position = {1.2f, 1.0f, 1.6f};
    view.camera.lookAt({0, 0, 0});
    std::vector<MeshDraw> draws;
    crate->model.appendDraws(draws);
    view.draws = draws;
    const auto image = f.render(view, {96, 96});
    CHECK(distinctColors(image) > 4);

    // Releasing the model frees its materials (its own + glTF default) after the GPU is done.
    const uint32_t freeBefore = materials.freeMaterialCount();
    view.draws = {};
    draws.clear();
    crate = {};
    crateLinear = {};
    sceneMaterials = {};
    Fixture::collect();
    CHECK(materials.freeMaterialCount() == freeBefore + 4 + 2);
    CHECK(assets.stats().models == 0);
    CHECK(assets.stats().textures == 0);
}
