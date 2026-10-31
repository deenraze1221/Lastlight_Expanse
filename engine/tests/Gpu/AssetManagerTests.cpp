// Phase C on a real device: AssetManager loading (blocking and async), caching, release and
// pixel-art sampling.
#include "GpuTestContext.h"

#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetRegistry.h"
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
