// Phase C on a real device: AssetManager loading (blocking and async), caching, release and
// pixel-art sampling.
#include "GpuTestContext.h"

#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetRegistry.h"
#include "AssetManager/LiveScene.h"
#include "AssetPipeline/ModelCompiler.h"
#include "Assets/Primitives.h"
#include "Core/ImageIO.h"
#include "Core/JobSystem.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/SceneRenderer.h"
#include "SceneFile/SceneFile.h"
#include "WorldRender/WorldRender.h"

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <set>
#include <tuple>
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
    CHECK((texture->sizeClass == TextureSizeClass::Default));
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
    CHECK(crate->warnings.empty()); // 32x32 texture: default size
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
    CHECK((rune->textures[0]->sizeClass == TextureSizeClass::Default));
    CHECK(materials.material(rune->handle).emissiveTexture.sampler == SamplerDesc::pixelArt());
    CHECK((sceneMaterials.byId.at("crateLinear")->textures.at(0)->sizeClass == TextureSizeClass::Default));
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

TEST_CASE("asset manager: async loads decode on workers and become ready through update/waitAll") {
    zodar::test::ValidationGuard guard;
    const AssetDir files;
    Fixture f;
    JobSystem jobs(2);
    AssetManagerDesc desc;
    desc.root = files.dir;
    desc.jobs = &jobs;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);

    TextureHandle texture = assets.loadTextureAsync("checker.png");
    MaterialAssetHandle pixel = assets.loadMaterialAsync("materials/pixel.material.json");
    ModelHandle crate = assets.loadModelAsync(kExampleDir / "crate.gltf");
    // Nothing is ready before update() hands the work over, whatever the workers did.
    CHECK(texture.loading());
    CHECK(pixel.loading());
    CHECK(crate.loading());
    CHECK_THROWS_AS((void)crate.get(), Error);
    CHECK(assets.loadTextureAsync("checker.png") == texture); // shared while loading
    CHECK(assets.stats().pending == 3); // texture decode, model decode, material

    assets.waitAll();
    CHECK(assets.pendingCount() == 0);
    REQUIRE(texture.ready());
    REQUIRE(pixel.ready());
    REQUIRE(crate.ready());
    CHECK(pixel->textures.at(0) == texture); // the material waited for the shared texture
    CHECK(texture->mipLevels == 6);
    CHECK(crate->model.meshCount() == 1);
    CHECK(f.scene.materials().material(crate->model.materials()[0]).baseColor.sampler.magFilter == Filter::Nearest);

    // Async results render exactly like blocking ones.
    std::optional<GpuMesh> plane;
    {
        UploadBatch batch(*gpu().upload);
        plane.emplace(batch, primitives::plane(2.0f, 2.0f));
        batch.submit();
    }
    std::vector<MeshDraw> draws;
    const auto asyncImage = f.render(planeView(*plane, pixel->handle, draws), {128, 96});
    CHECK(distinctColors(asyncImage) == 2);
    // A blocking load of a ready async asset is a cache hit.
    CHECK(assets.loadMaterial("materials/pixel.material.json") == pixel);
    gpu().context->waitIdle();
}

TEST_CASE("asset manager: async upload budget, blocking waits and cancellation") {
    zodar::test::ValidationGuard guard;
    const AssetDir files;
    Fixture f;
    JobSystem jobs(2);
    AssetManagerDesc desc;
    desc.root = files.dir;
    desc.jobs = &jobs;
    desc.uploadBudgetBytes = 1; // one upload per update()
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);

    std::vector<TextureHandle> textures{assets.loadTextureAsync("checker.png"),
                                        assets.loadTextureAsync("odd.png"),
                                        assets.loadTextureAsync("checker.png", {TextureUsage::Data, false})};
    jobs.waitIdle(); // every decode finished
    const auto started = [&] {
        size_t ready = 0;
        for (const TextureHandle& t : textures) {
            ready += t.ready() ? 1 : 0;
        }
        return assets.stats().uploading + ready;
    };
    assets.update();
    CHECK(started() == 1);
    assets.update();
    CHECK(started() == 2);
    // A blocking load of a loading asset waits for it (and everything else pending).
    const TextureHandle odd = assets.loadTexture("odd.png");
    CHECK(odd.ready());
    CHECK(odd == textures[1]);
    CHECK(assets.pendingCount() == 0);
    CHECK(textures[1]->warnings.size() == 1); // size warnings survive the async path
    textures.clear();

    // Cancel while decoding: dropped handles discard the result.
    TextureHandle canceled = assets.loadTextureAsync("checker.png");
    canceled = {};
    assets.waitAll();
    CHECK(assets.stats().textures == 1); // only `odd`
    // Cancel while uploading: the finished upload is retired like a released asset.
    TextureHandle inFlight = assets.loadTextureAsync("checker.png");
    jobs.waitIdle();
    assets.update();
    inFlight = {};
    assets.waitAll();
    CHECK(assets.stats().textures == 1);
    Fixture::collect();
}

TEST_CASE("asset manager: async failures carry messages") {
    zodar::test::ValidationGuard guard;
    const AssetDir files;
    Fixture f;
    JobSystem jobs(1);
    AssetManagerDesc desc;
    desc.root = files.dir;
    desc.jobs = &jobs;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);

    const TextureHandle missing = assets.loadTextureAsync("nope.png");
    const ModelHandle badModel = assets.loadModelAsync("checker.png");
    const MaterialAssetHandle badTexture = assets.loadMaterialAsync("materials/missing.material.json");
    const MaterialAssetHandle badShader = assets.loadMaterialAsync("materials/shader.material.json");
    const MaterialAssetHandle noFile = assets.loadMaterialAsync("materials/none.material.json");
    CHECK(badShader.failed()); // known at once
    CHECK(noFile.failed());
    const SceneDocument doc = parseScene(R"({"version": 1, "materials": {
        "a": {"baseColorTexture": "missing_a.png"}, "b": {"baseColorTexture": "checker.png"}}})",
                                         (files.dir / "s.scene.json").generic_string());
    const SceneMaterialAssets scene = assets.createSceneMaterialsAsync(doc, files.dir / "s.scene.json");
    CHECK(scene.loading());
    assets.waitAll();
    CHECK_FALSE(scene.loading());
    CHECK(missing.failed());
    CHECK(missing.error().find("nope.png") != std::string::npos);
    CHECK(badModel.failed());
    CHECK(badTexture.failed());
    CHECK(badTexture.error().find("baseColorTexture") != std::string::npos);
    REQUIRE(scene.errors().size() == 1);
    CHECK(scene.errors()[0].starts_with("a: "));
    CHECK(scene.byId.at("b").ready());
}

TEST_CASE("asset manager: pixel-art textures reach the GPU byte-exact (source PNG and compiled KTX2)") {
    zodar::test::ValidationGuard guard;
    // Work on a copy so the compiled cache does not land in the repository.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "zodar_pixel_integrity";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "textures");
    std::filesystem::copy_file(kExampleDir / "crate.gltf", dir / "crate.gltf");
    std::filesystem::copy_file(kExampleDir / "textures" / "crate.png", dir / "textures" / "crate.png");
    const ImageData reference = loadImage(dir / "textures" / "crate.png", ColorSpace::Srgb);

    Fixture f;
    AssetManagerDesc desc;
    desc.root = dir;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);
    const auto readMip0 = [&](const ModelHandle& h) {
        REQUIRE(h.ready());
        REQUIRE(h->model.textures().size() == 1);
        const Texture& t = h->model.textures()[0];
        CHECK(t.format() == VK_FORMAT_R8G8B8A8_SRGB); // never block-compressed
        return gpu().upload->readImage(t.image(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };
    {
        const ModelHandle source = assets.loadModel("crate.gltf");
        CHECK(readMip0(source) == reference.levels[0]);
    }
    // Compiled cache (crate.gltf uses a NEAREST sampler: lossless RGBA8 KTX2).
    const auto compiled = assets::compileModelTextures(dir / "crate.gltf");
    REQUIRE(compiled.size() == 1);
    CHECK((compiled[0].format == PixelFormat::Rgba8));
    CHECK(loadGltf(dir / "crate.gltf").textureStats.compiled == 1); // the cache is used
    Fixture::collect();
    ModelLoadSettings settings;
    settings.samplerOverride = SamplerDesc::pixelArt(); // separate cache entry: loads the KTX2
    const ModelHandle fromCache = assets.loadModel("crate.gltf", settings);
    CHECK(readMip0(fromCache) == reference.levels[0]);
    gpu().context->waitIdle();
    std::filesystem::remove_all(dir);
}

TEST_CASE("scene file: modelRenderer by registry id and by path renders the loaded models") {
    zodar::test::ValidationGuard guard;
    Fixture f;
    AssetManagerDesc desc;
    desc.defaultSampler = SamplerDesc::pixelArt();
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);
    const std::filesystem::path scenePath = kExampleDir / "asset_loading.scene.json";
    const SceneDocument doc = loadSceneFile(scenePath);
    const AssetRegistry registry = AssetRegistry::load(kExampleDir / "assets.json");

    // Without a registry the id cannot be resolved; the error names the reference.
    try {
        (void)assets.loadSceneModels(doc, scenePath);
        FAIL("expected DataError");
    } catch (const DataError& e) {
        REQUIRE(e.issues().size() == 1);
        CHECK(e.issues()[0].message.find("modelRenderer crate: not a model file") == 0);
    }
    const SceneModelAssets models = assets.loadSceneModels(doc, scenePath, &registry);
    REQUIRE(models.byReference.size() == 2);
    const ModelHandle& byId = models.byReference.at("crate");
    const ModelHandle& byPath = models.byReference.at("crate.gltf");
    REQUIRE(byId.ready());
    REQUIRE(byPath.ready());
    CHECK(byId->path == byPath->path); // same file, different load settings (registry: compile=false)
    // The async variant shares the cached models.
    const SceneModelAssets again = assets.loadSceneModelsAsync(doc, scenePath, &registry);
    CHECK(again.byReference.at("crate") == byId);
    CHECK_FALSE(again.loading());

    const SceneMaterialAssets materials = assets.createSceneMaterials(doc, scenePath);
    std::optional<GpuMesh> cube;
    {
        UploadBatch batch(*gpu().upload);
        cube.emplace(batch, primitives::cube(1.0f));
        batch.submit();
    }
    SceneBindings bindings;
    bindings.meshes = {{"cube", &*cube}};
    bindings.materials = materials.handles();
    bindings.models = models.models();
    World world;
    const SceneInstance instance = instantiateScene(doc, world, bindings);
    const Entity crate = instance.byId.at("crate");
    CHECK(world.get<ModelRenderer>(crate).model == &byId->model);

    std::vector<MeshDraw> draws;
    collectDraws(world, draws);
    REQUIRE(draws.size() == 4); // 2 cubes (MeshRenderer) + 2 crates (1 submesh each)
    std::vector<MeshDraw> expected(draws.begin(), draws.begin() + 2);
    byId->model.appendDraws(expected, world.worldMatrix(crate));
    for (const Entity e : instance.entities) {
        if (const ModelRenderer* r = world.tryGet<ModelRenderer>(e); r && e != crate) {
            byPath->model.appendDraws(expected, world.worldMatrix(e));
        }
    }
    REQUIRE(expected.size() == 4);
    // Same draws; order follows World::each, so compare sorted by position.
    const auto byPosition = [](const MeshDraw& a, const MeshDraw& b) {
        return std::tie(a.transform[3].x, a.transform[3].y, a.transform[3].z) <
               std::tie(b.transform[3].x, b.transform[3].y, b.transform[3].z);
    };
    std::vector<MeshDraw> sortedDraws = draws;
    std::ranges::sort(sortedDraws, byPosition);
    std::ranges::sort(expected, byPosition);
    draws = sortedDraws;
    for (size_t i = 0; i < 4; ++i) {
        CHECK(draws[i].mesh == expected[i].mesh);
        CHECK(draws[i].material == expected[i].material);
        CHECK(draws[i].transform == expected[i].transform);
    }
    SceneView view;
    collectLighting(world, view);
    collectCamera(world, view.camera);
    view.draws = draws;
    const auto image = f.render(view, {160, 90});
    view.draws = expected;
    CHECK(f.render(view, {160, 90}) == image);
    // Hidden model entities draw nothing; GpuScene sync ignores ModelRenderer (CPU path only).
    world.get<ModelRenderer>(crate).visible = false;
    draws.clear();
    collectDraws(world, draws);
    CHECK(draws.size() == 3);
    gpu().context->waitIdle();
}

// --- Hot reload (Phase C5) ------------------------------------------------------------------

namespace {

// Temporary folder for hot-reload tests: solid-colour textures and material files.
struct ReloadDir {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "zodar_hot_reload_tests";

    ReloadDir() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~ReloadDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::filesystem::path write(const std::string& name, const std::string& text) const {
        std::ofstream(dir / name, std::ios::binary | std::ios::trunc) << text;
        return dir / name;
    }
    std::filesystem::path solid(const std::string& name, uint8_t r, uint8_t g, uint8_t b) const {
        std::vector<uint8_t> pixels(32 * 32 * 4);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = 255;
        }
        REQUIRE(writePng(dir / name, 32, 32, pixels));
        return dir / name;
    }
    static std::string unlit(const std::string& texture, const std::string& extra = "") {
        return R"({"shader": "zodar/unlit", )" + extra + R"("baseColorTexture": {"path": ")" + texture +
               R"(", "filter": "pixelArt"}})";
    }
};

// Index of the strongest channel of the centre pixel (0 = red, 1 = green, 2 = blue).
int dominantChannel(const std::vector<uint8_t>& rgba, VkExtent2D extent) {
    const uint8_t* p = &rgba[((extent.height / 2) * extent.width + extent.width / 2) * 4];
    return p[0] > p[1] && p[0] > p[2] ? 0 : (p[1] > p[2] ? 1 : 2);
}

GpuMesh makePlane() {
    UploadBatch batch(*gpu().upload);
    GpuMesh plane(batch, primitives::plane(2.0f, 2.0f));
    batch.submit();
    return plane;
}

} // namespace

TEST_CASE("hot reload: textures and material files reload in place; failures keep the previous version") {
    zodar::test::ValidationGuard guard;
    const ReloadDir files;
    files.solid("tex.png", 255, 0, 0);
    files.solid("green.png", 0, 255, 0);
    files.write("mat.material.json", ReloadDir::unlit("tex.png"));
    Fixture f;
    AssetManagerDesc desc;
    desc.root = files.dir;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);
    MaterialSystem& materials = f.scene.materials();
    const GpuMesh plane = makePlane();
    const VkExtent2D extent{64, 48};
    std::vector<MeshDraw> draws;

    const MaterialAssetHandle mat = assets.loadMaterial("mat.material.json");
    REQUIRE(mat.ready());
    const TextureHandle tex = mat->textures.at(0);
    const TextureAsset* texAddress = &tex.get();
    const MaterialAsset* matAddress = &mat.get();
    const MaterialHandle gfxHandle = mat->handle;
    const uint32_t oldSlot = tex->texture.bindlessIndex();
    CHECK(dominantChannel(f.render(planeView(plane, gfxHandle, draws), extent), extent) == 0);

    SUBCASE("texture change refreshes the material that uses it") {
        files.solid("tex.png", 0, 0, 255);
        const AssetReloadResult r = assets.reloadFiles({files.dir / "tex.png"});
        CHECK(r.ok());
        CHECK(r.reloaded.size() == 1);
        CHECK(r.materialsRefreshed == 1);
        CHECK(&tex.get() == texAddress); // same object, new GPU texture
        CHECK(tex.generation() == 1);
        CHECK(tex->texture.bindlessIndex() != oldSlot);
        CHECK(materials.hasPendingUpdates());
        CHECK(dominantChannel(f.render(planeView(plane, gfxHandle, draws), extent), extent) == 2);

        // A broken file: reload fails, the previous texture stays in use.
        files.write("tex.png", "not a png");
        const AssetReloadResult broken = assets.reload(tex);
        CHECK_FALSE(broken.ok());
        REQUIRE(broken.failed.size() == 1);
        CHECK(broken.failed[0].find("tex.png") != std::string::npos);
        CHECK(tex.ready());
        CHECK(tex.generation() == 1);
        CHECK(dominantChannel(f.render(planeView(plane, gfxHandle, draws), extent), extent) == 2);
        CHECK(assets.stats().reloads == 1);
        CHECK(assets.stats().reloadFailures == 1);
    }

    SUBCASE("material file change: new texture, same gfx material; invalid edits are rejected") {
        files.write("mat.material.json", ReloadDir::unlit("green.png"));
        const AssetReloadResult r = assets.reloadFiles({files.dir / "mat.material.json"});
        CHECK(r.ok());
        CHECK(&mat.get() == matAddress);
        CHECK(mat->handle == gfxHandle);
        CHECK(mat.generation() == 1);
        CHECK(mat->textures.at(0)->path.filename() == "green.png");
        CHECK(dominantChannel(f.render(planeView(plane, gfxHandle, draws), extent), extent) == 1);

        files.write("mat.material.json", ReloadDir::unlit("green.png", R"("alphaMode": "blend", )"));
        AssetReloadResult rejected = assets.reload(mat);
        REQUIRE(rejected.failed.size() == 1);
        CHECK(rejected.failed[0].find("pipeline state") != std::string::npos);
        files.write("mat.material.json", R"({"shader": )");
        rejected = assets.reload(mat);
        CHECK(rejected.failed.size() == 1);
        files.write("mat.material.json", ReloadDir::unlit("missing.png"));
        rejected = assets.reload(mat);
        CHECK(rejected.failed.size() == 1);
        CHECK(mat.ready());
        CHECK(mat.generation() == 1);
        CHECK(dominantChannel(f.render(planeView(plane, gfxHandle, draws), extent), extent) == 1);
    }

    SUBCASE("a failed material recovers once its texture exists") {
        files.write("late.material.json", ReloadDir::unlit("late.png"));
        const MaterialAssetHandle late = assets.loadMaterial("late.material.json");
        REQUIRE(late.failed());
        files.solid("late.png", 0, 255, 0);
        const AssetReloadResult r = assets.reloadFiles({files.dir / "late.png"});
        CHECK(r.ok());
        REQUIRE(late.ready());
        CHECK(late.generation() == 1);
        CHECK(dominantChannel(f.render(planeView(plane, late->handle, draws), extent), extent) == 1);

        files.write("mat.material.json", ReloadDir::unlit("green.png"));
        CHECK(assets.reload(mat).ok());
        CHECK(assets.stats().textures == 3); // tex.png (held by `tex`), green.png, late.png
        f.render(planeView(plane, gfxHandle, draws), extent);
        Fixture::collect();
    }
    gpu().context->waitIdle();
}

TEST_CASE("hot reload: data materials update atomically") {
    zodar::test::ValidationGuard guard;
    const ReloadDir files;
    files.solid("red.png", 255, 0, 0);
    files.solid("blue.png", 0, 0, 255);
    Fixture f;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, {});
    MaterialSystem& materials = f.scene.materials();
    MaterialData a;
    a.shader = "zodar/unlit";
    a.baseColorTexture.path = "red.png";
    MaterialData b = a;
    const MaterialAssetHandle ma = assets.createMaterial(a, files.dir, "scene#a");
    const MaterialAssetHandle mb = assets.createMaterial(b, files.dir, "scene#b");
    REQUIRE(ma.ready());
    REQUIRE(mb.ready());
    CHECK(assets.reload(ma).failed.size() == 1); // not from a file

    MaterialData a2 = a;
    a2.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
    a2.baseColorTexture.path = "blue.png";
    MaterialData b2 = b;
    b2.baseColorTexture.path = "missing.png"; // fails: nothing changes
    AssetReloadResult r = assets.updateMaterialData({{ma, a2, files.dir}, {mb, b2, files.dir}});
    CHECK(r.failed.size() == 1);
    CHECK(r.reloaded.empty());
    CHECK(ma.generation() == 0);
    CHECK(materials.material(ma->handle).baseColorFactor == glm::vec4(1.0f));

    r = assets.updateMaterialData({{ma, a2, files.dir}});
    CHECK(r.ok());
    CHECK(ma.generation() == 1);
    CHECK(ma->data == a2);
    CHECK(materials.material(ma->handle).baseColorFactor == glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
    CHECK(materials.material(ma->handle).baseColor.texture == &ma->textures.at(0)->texture);
    gpu().context->waitIdle();
}

TEST_CASE("hot reload: glTF models reload in place, with their texture files as dependencies") {
    zodar::test::ValidationGuard guard;
    const ReloadDir files;
    std::filesystem::copy(kExampleDir / "crate.gltf", files.dir / "crate.gltf");
    std::filesystem::create_directories(files.dir / "textures");
    std::filesystem::copy(kExampleDir / "textures" / "crate.png", files.dir / "textures" / "crate.png");
    std::string original;
    {
        std::ifstream in(files.dir / "crate.gltf", std::ios::binary);
        original.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    Fixture f;
    AssetManagerDesc desc;
    desc.root = files.dir;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);
    MaterialSystem& materials = f.scene.materials();

    const ModelHandle crate = assets.loadModel("crate.gltf");
    REQUIRE(crate.ready());
    const gfx::Model* address = &crate->model;
    const std::vector<std::filesystem::path> watched = assets.watchedFiles();
    CHECK(std::ranges::find(watched, files.dir / "textures" / "crate.png") != watched.end());
    CHECK(std::ranges::find(watched, files.dir / "crate.gltf") != watched.end());

    // Texture file of the model changed: the model reloads.
    const auto inUse = [&] { return materials.materialCount() - materials.freeMaterialCount(); };
    const uint32_t inUseBefore = inUse();
    AssetReloadResult r = assets.reloadFiles({files.dir / "textures" / "crate.png"});
    CHECK(r.ok());
    CHECK(r.reloaded == std::vector<std::string>{crate.key()});
    CHECK(&crate->model == address);
    CHECK(crate.generation() == 1);
    CHECK(crate->model.meshCount() == 1);

    // Broken file: previous model kept and still drawable.
    files.write("crate.gltf", "{ broken");
    r = assets.reload(crate);
    CHECK(r.failed.size() == 1);
    CHECK(crate.ready());
    CHECK(crate.generation() == 1);
    SceneView view;
    view.camera.position = {1.2f, 1.0f, 1.6f};
    view.camera.lookAt({0, 0, 0});
    std::vector<MeshDraw> draws;
    crate->model.appendDraws(draws);
    view.draws = draws;
    CHECK(distinctColors(f.render(view, {64, 64})) > 4);

    files.write("crate.gltf", original);
    CHECK(assets.reloadFiles({files.dir / "crate.gltf"}).ok());
    CHECK(crate.generation() == 2);
    // The two replaced models released their materials (own + glTF default) after the GPU.
    Fixture::collect();
    CHECK(inUse() == inUseBefore);
    gpu().context->waitIdle();
}

TEST_CASE("hot reload: opt-in file watching reloads changed files in update()") {
    zodar::test::ValidationGuard guard;
    const ReloadDir files;
    files.solid("tex.png", 255, 0, 0);
    Fixture f;
    AssetManagerDesc desc;
    desc.root = files.dir;
    desc.watchInterval = std::chrono::milliseconds(0);
    {
        AssetManager off(*gpu().context, *gpu().upload, f.scene, desc); // watchFiles = false (default)
        CHECK_FALSE(off.watching());
        CHECK(off.pollFileChanges().empty());
    }
    desc.watchFiles = true;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);
    CHECK(assets.watching() == kHotReloadAvailable);
    if (!kHotReloadAvailable) {
        MESSAGE("file watching not compiled in (non-Debug build or ZODAR_HOT_RELOAD=OFF)");
        return;
    }
    const TextureHandle tex = assets.loadTexture("tex.png");
    REQUIRE(tex.ready());
    // The baseline is taken when the file is read: an edit before the first poll is seen.
    files.solid("tex.png", 0, 255, 0);
    std::filesystem::last_write_time(files.dir / "tex.png",
                                     std::filesystem::last_write_time(files.dir / "tex.png") + std::chrono::seconds(1));
    assets.update();
    CHECK(tex.generation() == 1);
    files.solid("tex.png", 0, 0, 255);
    std::filesystem::last_write_time(files.dir / "tex.png",
                                     std::filesystem::last_write_time(files.dir / "tex.png") + std::chrono::seconds(2));
    assets.update();
    CHECK(tex.generation() == 2);
    assets.update(); // no further change
    CHECK(tex.generation() == 2);
    gpu().context->waitIdle();
}

// --- Shader and scene hot reload (Phase C6) -------------------------------------------------

TEST_CASE("hot reload: material shaders reload from SPIR-V and rebuild their pipelines in place") {
    zodar::test::ValidationGuard guard;
    const ReloadDir files;
    const std::filesystem::path shaders = zodar::test::shaderDir();
    std::filesystem::copy(shaders / "mesh.vert.spv", files.dir / "reload.vert.spv");
    std::filesystem::copy(shaders / "unlit.frag.spv", files.dir / "reload.frag.spv");
    const std::filesystem::path frag = files.dir / "reload.frag.spv";
    const auto replaceFrag = [&](const std::filesystem::path& from) {
        std::filesystem::copy_file(from, frag, std::filesystem::copy_options::overwrite_existing);
    };
    Fixture f;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, {});
    MaterialSystem& materials = f.scene.materials();
    const ShaderHandle shader =
        materials.registerShader("test/reload", files.dir / "reload.vert.spv", files.dir / "reload.frag.spv");
    CHECK(materials.fragmentFile(shader) == frag);
    MaterialDesc desc;
    desc.name = "reloadable";
    desc.shader = shader;
    desc.baseColorFactor = {1, 0, 0, 1};
    const MaterialHandle material = materials.createMaterial(desc);
    const GpuMesh plane = makePlane();
    const VkExtent2D extent{64, 48};
    std::vector<MeshDraw> draws;
    const auto unlitImage = f.render(planeView(plane, material, draws), extent);
    CHECK(dominantChannel(unlitImage, extent) == 0);
    const GraphicsPipeline* pipeline = &materials.pipeline(material);
    const VkPipeline oldHandle = pipeline->handle();
    const auto watched = assets.watchedFiles();
    CHECK(std::ranges::find(watched, frag) != watched.end());

    // New SPIR-V (lit shader instead of unlit): same GraphicsPipeline object, new VkPipeline.
    replaceFrag(shaders / "pbr.frag.spv");
    AssetReloadResult r = assets.reloadFiles({frag});
    REQUIRE(r.ok());
    CHECK(r.reloaded == std::vector<std::string>{"shader:test/reload"});
    CHECK(&materials.pipeline(material) == pipeline);
    const VkPipeline newHandle = pipeline->handle();
    CHECK(newHandle != oldHandle);
    const auto litImage = f.render(planeView(plane, material, draws), extent);
    CHECK(litImage != unlitImage);

    // Invalid files: nothing changes.
    files.write("reload.frag.spv", "garbage that is not SPIR-V");
    r = assets.reloadShader("test/reload");
    REQUIRE(r.failed.size() == 1);
    CHECK(r.failed[0].find("SPIR-V") != std::string::npos);
    replaceFrag(shaders / "mesh.vert.spv"); // valid SPIR-V, wrong stage
    r = assets.reloadShader("test/reload");
    REQUIRE(r.failed.size() == 1);
    CHECK(r.failed[0].find("fragment entry point") != std::string::npos);
    {
        std::string bytes;
        {
            std::ifstream in(shaders / "pbr.frag.spv", std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        files.write("reload.frag.spv", bytes.substr(0, (bytes.size() / 2) & ~size_t(3))); // truncated
    }
    r = assets.reloadShader("test/reload");
    REQUIRE(r.failed.size() == 1);
    CHECK(r.failed[0].find("truncated") != std::string::npos);
    std::filesystem::remove(frag);
    CHECK(materials.reloadShader(shader, gpu().context->deletionQueue()).error.find("Cannot open") != std::string::npos);
    CHECK_FALSE(materials.reloadShader(999, gpu().context->deletionQueue()).ok);
    CHECK(assets.reloadShader("test/unknown").failed.size() == 1);
    CHECK(pipeline->handle() == newHandle);
    CHECK(f.render(planeView(plane, material, draws), extent) == litImage);

    // Back to unlit: the retired pipelines were destroyed after the GPU finished (validation
    // would report a destroyed pipeline still in use).
    replaceFrag(shaders / "unlit.frag.spv");
    CHECK(assets.reloadShader("test/reload").ok());
    CHECK(f.render(planeView(plane, material, draws), extent) == unlitImage);
    Fixture::collect();
    CHECK(assets.stats().reloads == 2); // the two successful shader reloads
    gpu().context->waitIdle();
}

namespace {

std::string sceneJson(const std::string& position, const std::string& material, const std::string& mesh = "plane",
                      const std::string& materialRef = "m") {
    return R"({"version": 1, "materials": {"m": )" + material + R"(}, "entities": [
        {"id": "a", "name": "A", "transform": {"position": )" + position +
           R"(}, "meshRenderer": {"mesh": ")" + mesh + R"(", "material": ")" + materialRef + R"("}},
        {"id": "lamp", "prefab": "lamp.prefab.json"}]})";
}
const std::string kRed = R"({"shader": "zodar/unlit", "baseColor": [1, 0, 0, 1]})";
const std::string kGreen = R"({"shader": "zodar/unlit", "baseColor": [0, 1, 0, 1]})";

std::string lampJson(int intensity) {
    return R"({"version": 1, "entities": [{"id": "bulb", "pointLight": {"intensity": )" + std::to_string(intensity) +
           "}}]}";
}

} // namespace

TEST_CASE("hot reload: live scenes reload changed scene and prefab files; failures keep the scene") {
    zodar::test::ValidationGuard guard;
    const ReloadDir files;
    files.write("level.scene.json", sceneJson("[1, 0, 0]", kRed));
    files.write("lamp.prefab.json", lampJson(10));
    Fixture f;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, {});
    MaterialSystem& materials = f.scene.materials();
    const GpuMesh plane = makePlane();
    World world;
    {
        LiveSceneDesc desc;
        desc.meshes["plane"] = &plane;
        desc.watch = true;
        desc.watchInterval = std::chrono::milliseconds(0);
        LiveScene scene(assets, world, files.dir / "level.scene.json", desc);
        CHECK_FALSE(scene.loaded());
        REQUIRE_MESSAGE(scene.load(), scene.error());
        CHECK(scene.generation() == 1);
        CHECK(scene.files() == std::vector<std::filesystem::path>{files.dir / "level.scene.json",
                                                                  files.dir / "lamp.prefab.json"});
        const size_t entityCount = world.entityCount();
        const Entity first = scene.instance().byId.at("a");
        CHECK(world.get<Transform>(first).position.x == 1.0f);
        const MaterialHandle mh = scene.bindings().materials.at("m");

        // Transform + inline material change: new entities, same gfx material, new data.
        files.write("level.scene.json", sceneJson("[2, 0, 0]", kGreen));
        REQUIRE_MESSAGE(scene.reload(), scene.error());
        CHECK(scene.generation() == 2);
        CHECK_FALSE(world.isAlive(first));
        CHECK(world.entityCount() == entityCount);
        const Entity second = scene.instance().byId.at("a");
        CHECK(world.get<Transform>(second).position.x == 2.0f);
        CHECK(world.get<MeshRenderer>(second).material == mh);
        CHECK(scene.bindings().materials.at("m") == mh);
        CHECK(materials.material(mh).baseColorFactor == glm::vec4(0, 1, 0, 1));

        // Failures: syntax, unknown material id, unknown mesh, pipeline-state change, missing
        // prefab. The scene, its entities and the material stay as they were.
        const std::vector<std::string> broken = {
            R"({"version": 1, "entities": [)",
            sceneJson("[3, 0, 0]", kGreen, "plane", "nope"),
            sceneJson("[3, 0, 0]", kGreen, "nothing"),
            sceneJson("[3, 0, 0]", R"({"shader": "zodar/pbr", "baseColor": [0, 0, 1, 1]})"),
        };
        for (const std::string& text : broken) {
            files.write("level.scene.json", text);
            CHECK_FALSE(scene.reload());
            CHECK_FALSE(scene.error().empty());
            CHECK(scene.generation() == 2);
            CHECK(world.isAlive(second));
            CHECK(world.entityCount() == entityCount);
            CHECK(world.get<Transform>(second).position.x == 2.0f);
            CHECK(materials.material(mh).baseColorFactor == glm::vec4(0, 1, 0, 1));
        }
        CHECK(scene.error().find("pipeline state") != std::string::npos);

        // Prefab changes reload the scene through watching (Debug builds) or reload().
        files.write("level.scene.json", sceneJson("[2, 0, 0]", kGreen));
        REQUIRE(scene.reload());
        scene.update(); // records the watched files' state
        files.write("lamp.prefab.json", lampJson(20));
        std::filesystem::last_write_time(files.dir / "lamp.prefab.json",
                                         std::filesystem::last_write_time(files.dir / "lamp.prefab.json") +
                                             std::chrono::seconds(2));
        if (kHotReloadAvailable) {
            CHECK(scene.update());
            CHECK_FALSE(scene.update()); // nothing new
        } else {
            CHECK_FALSE(scene.update());
            CHECK(scene.reload());
        }
        CHECK(scene.generation() == 4);
        CHECK(world.get<PointLightComponent>(scene.instance().byId.at("lamp/bulb")).intensity == 20.0f);

        files.write("lamp.prefab.json", "{");
        CHECK_FALSE(scene.reload());
        CHECK(scene.error().find("lamp.prefab.json") != std::string::npos);
        CHECK(scene.files().size() == 2); // the broken prefab stays watched
        CHECK(world.get<PointLightComponent>(scene.instance().byId.at("lamp/bulb")).intensity == 20.0f);

        std::vector<MeshDraw> draws;
        SceneView view = planeView(plane, mh, draws);
        CHECK(dominantChannel(f.render(view, {64, 48}), {64, 48}) == 1);
    }
    CHECK(world.entityCount() == 0); // the LiveScene destroyed its entities
    Fixture::collect();
    gpu().context->waitIdle();
}

TEST_CASE("hot reload: changes during async loads are deferred; '#' material files; empty handles") {
    zodar::test::ValidationGuard guard;
    const ReloadDir files;
    files.solid("tex.png", 255, 0, 0);
    files.write("a#1.material.json", ReloadDir::unlit("tex.png"));
    Fixture f;
    AssetManagerDesc desc;
    desc.root = files.dir;
    AssetManager assets(*gpu().context, *gpu().upload, f.scene, desc);

    CHECK(assets.reload(TextureHandle{}).empty()); // no throw
    CHECK(assets.reload(ModelHandle{}).empty());
    CHECK(assets.reload(MaterialAssetHandle{}).empty());

    // Decoded (inline, no jobs) but the upload is pending: the change is retried in update().
    const TextureHandle tex = assets.loadTextureAsync("tex.png");
    REQUIRE(tex.loading());
    files.solid("tex.png", 0, 0, 255);
    CHECK(assets.reloadFiles({files.dir / "tex.png"}).empty());
    assets.waitAll();
    REQUIRE(tex.ready());
    CHECK(tex.generation() == 0);
    assets.update();
    CHECK(tex.generation() == 1);
    assets.update();
    CHECK(tex.generation() == 1); // retried once only

    // A '#' in a material file name is not an inline material: path kept, reloadable.
    const MaterialAssetHandle mat = assets.loadMaterial("a#1.material.json");
    REQUIRE(mat.ready());
    CHECK(mat->path.filename() == "a#1.material.json");
    const AssetReloadResult r = assets.reloadFiles({files.dir / "a#1.material.json"});
    CHECK(r.ok());
    CHECK(mat.generation() == 1);
    gpu().context->waitIdle();
}
