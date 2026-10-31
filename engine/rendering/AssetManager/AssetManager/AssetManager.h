#pragma once

#include "Assets/AssetCache.h"
#include "Assets/ModelData.h"
#include "Assets/TextureCache.h"
#include "Assets/TextureStandards.h"
#include "Core/JobSystem.h"
#include "Renderer/Resources/UploadQueue.h"
#include "Renderer/Material.h"
#include "Renderer/Model.h"
#include "Renderer/Sampler.h"
#include "Renderer/Texture.h"
#include "SceneFile/MaterialFile.h"
#include "SceneFile/SceneFile.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Runtime asset manager (Phase C). Loads textures (PNG/JPEG/TGA/BMP), glTF 2.0 models
// (.gltf/.glb) and material files (*.material.json with textures) into GPU resources,
// caches them by resolved path + settings and shares them through reference-counted
// handles. Guide: docs/guide/ASSETS.md.
//
//   AssetManagerDesc desc;
//   desc.root = "assets";
//   desc.defaultSampler = gfx::SamplerDesc::pixelArt(); // a pixel-art game's choice
//   AssetManager assets(context, upload, scene, desc);
//   ModelHandle crate = assets.loadModel("crate.gltf");
//   if (crate.failed()) { log(crate.error()); }                // never throws for bad files
//   crate->model.appendDraws(view.draws, transform);
//   MaterialAssetHandle m = assets.loadMaterial("crate.material.json");
//
// Lifetime: dropping the last handle retires the asset on the device deletion queue (the
// frames in flight finish first); textures release their bindless slot, materials their
// MaterialSystem slot (reused by later materials). Material assets keep their textures alive.
// Loading: load*() block until the asset is usable (one UploadBatch per asset). load*Async()
// (Phase C2) return at once with a loading() handle: files are decoded on the JobSystem
// (AssetManagerDesc::jobs), GPU uploads go through the UploadQueue (submitAsync, limited per
// update() by uploadBudgetBytes) and the asset turns ready() in update() once its upload was
// acquired by a frame. Draw only ready() assets. waitAll() blocks until nothing is pending.
//   ModelHandle h = assets.loadModelAsync("crate.glb");   // onStart
//   assets.update();                                       // every frame (onUpdate)
//   if (h.ready()) h->model.appendDraws(draws);            // draw once ready
// Dropping every handle of a loading asset cancels it (decoded data is discarded; a finished
// upload is retired like any released asset).
// Models are uploaded as standalone meshes (CPU draw path): GeometryPool cannot free meshes.
//
// Rules: use from the render thread. Release every handle before the AssetManager is
// destroyed; the destructor waits for the device and runs the retired destructions.
// Declare the AssetManager before (destroyed after) members holding handles.
namespace zodar {

struct TextureAsset {
    gfx::Texture texture;
    std::filesystem::path path;
    TextureUsage usage = TextureUsage::Color;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    TextureSizeClass sizeClass = TextureSizeClass::NonStandard;
    std::vector<std::string> warnings; // e.g. non-standard size
};

struct ModelAsset {
    gfx::Model model;
    std::filesystem::path path;
    std::vector<std::string> warnings; // importer warnings + texture size warnings
};

struct MaterialAsset {
    gfx::MaterialHandle handle = 0;
    MaterialData data;
    std::filesystem::path path; // material file, or the scene file for inline materials
    std::vector<AssetHandle<TextureAsset>> textures; // bound textures, kept alive
};

using TextureHandle = AssetHandle<TextureAsset>;
using ModelHandle = AssetHandle<ModelAsset>;
using MaterialAssetHandle = AssetHandle<MaterialAsset>;

struct AssetManagerDesc {
    // Relative paths resolve against this directory (empty: the working directory).
    std::filesystem::path root;
    // Sampler for textures whose material or file leaves filtering open. Engine default is
    // linear; pixel-art games pass gfx::SamplerDesc::pixelArt().
    gfx::SamplerDesc defaultSampler = gfx::SamplerDesc::linear();
    // Warn (log + asset warnings) about textures outside the size standards
    // (Assets/TextureStandards.h: 32, 64, 128, 256 square).
    bool checkTextureSizes = true;
    // Worker pool decoding async loads (e.g. Application::jobs()); must outlive the manager's
    // pending loads. nullptr (or a pool with 0 workers): async loads decode inline in the
    // load call; the GPU upload is still asynchronous.
    JobSystem* jobs = nullptr;
    // Async uploads started per update(): stops once this many bytes were staged (at least
    // one asset per update, so large assets still progress).
    uint64_t uploadBudgetBytes = 32ull << 20;
};

struct TextureLoadSettings {
    TextureUsage usage = TextureUsage::Color; // colour space and mip filter
    bool generateMips = true;
};

struct ModelLoadSettings {
    // Replaces every sampler of the model (e.g. force pixel-art sampling); otherwise the
    // file's glTF samplers apply, and defaultSampler where the file specifies no filter.
    std::optional<gfx::SamplerDesc> samplerOverride;
    // Use compiled KTX2 textures from the model's cache directory when up to date.
    bool useCompiledTextures = true;
    std::optional<gfx::ShaderHandle> pbrShader;
    std::optional<gfx::ShaderHandle> unlitShader;
};

// Materials of a scene document, created through the AssetManager (textures included).
struct SceneMaterialAssets {
    std::unordered_map<std::string, MaterialAssetHandle> byId;
    // Material handles by scene material id, for SceneBindings::materials. Every material
    // must be ready (throws zodar::Error otherwise).
    std::unordered_map<std::string, gfx::MaterialHandle> handles() const;
    // Async creation (createSceneMaterialsAsync): any material still loading.
    bool loading() const;
    // "<id>: <error>" for every failed material, sorted by id.
    std::vector<std::string> errors() const;
};

class AssetManager {
public:
    AssetManager(gfx::VulkanContext& context, gfx::UploadContext& upload, gfx::SceneRenderer& scene,
                 AssetManagerDesc desc = {});
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Each load returns the cached asset when the same resolved path (and settings) is loaded,
    // otherwise loads it. Errors never throw: the handle is failed() with error().
    TextureHandle loadTexture(const std::filesystem::path& path, const TextureLoadSettings& settings = {});
    ModelHandle loadModel(const std::filesystem::path& path, const ModelLoadSettings& settings = {});
    // *.material.json; texture paths are relative to the material file. A texture that fails
    // to load fails the material.
    MaterialAssetHandle loadMaterial(const std::filesystem::path& path);
    // Material from data (e.g. inline scene materials). Texture paths resolve against
    // `baseDirectory`; cached under `key` (must be unique, e.g. "<scene path>#<id>").
    MaterialAssetHandle createMaterial(const MaterialData& data, const std::filesystem::path& baseDirectory,
                                       const std::string& key);
    // Every material of a loaded scene document: file-referenced ones via loadMaterial
    // (relative to the scene file's directory), inline ones via createMaterial. Throws
    // DataError (all failures listed) if any material fails; handles created so far are released.
    SceneMaterialAssets createSceneMaterials(const SceneDocument& document, const std::filesystem::path& sceneFile);

    // Asynchronous variants (same caching and keys; see the header comment). A blocking load
    // of an asset that is loading asynchronously waits for it (waitAll).
    TextureHandle loadTextureAsync(const std::filesystem::path& path, const TextureLoadSettings& settings = {});
    ModelHandle loadModelAsync(const std::filesystem::path& path, const ModelLoadSettings& settings = {});
    // The material file is parsed at once (small); the material becomes ready when its
    // textures are ready, and fails when one of them fails.
    MaterialAssetHandle loadMaterialAsync(const std::filesystem::path& path);
    MaterialAssetHandle createMaterialAsync(const MaterialData& data, const std::filesystem::path& baseDirectory,
                                            const std::string& key);
    // Never throws: check result.loading() / result.errors() later.
    SceneMaterialAssets createSceneMaterialsAsync(const SceneDocument& document,
                                                  const std::filesystem::path& sceneFile);

    // Advances async loads (render thread, once per frame before recording; the frame loop's
    // acquire makes uploads ready): takes decoded files, starts uploads within the budget,
    // marks acquired uploads ready, creates materials whose textures are ready.
    void update();
    // Blocks until every async load is ready or failed (waits for the JobSystem and uploads).
    void waitAll();
    // Async loads not finished yet (decoding, waiting for budget, uploading, materials).
    size_t pendingCount() const;

    // Absolute, normalised path used as cache key.
    std::filesystem::path resolve(const std::filesystem::path& path) const;
    const AssetManagerDesc& desc() const { return m_desc; }

    struct Stats {
        size_t textures = 0; // live entries
        size_t models = 0;
        size_t materials = 0;
        uint64_t loads = 0;    // load attempts (all types)
        uint64_t hits = 0;     // answered from the cache
        uint64_t failures = 0; // failed loads
        uint64_t released = 0; // entries whose last handle was dropped
        size_t pending = 0;    // async loads in flight (pendingCount())
        size_t uploading = 0;  // ... of which GPU uploads not yet ready
    };
    Stats stats() const;

private:
    struct DecodedTexture;
    struct DecodedModel;
    struct DecodeQueue;
    struct Decoded;
    struct TextureRequest {
        TextureHandle handle;
        TextureLoadSettings settings;
        std::filesystem::path file;
    };
    struct ModelRequest {
        ModelHandle handle;
        ModelLoadSettings settings;
        std::filesystem::path file;
    };
    struct TextureUpload {
        TextureHandle handle;
        std::unique_ptr<TextureAsset> staged;
        gfx::UploadTicket ticket;
    };
    struct ModelUpload {
        ModelHandle handle;
        std::unique_ptr<ModelAsset> staged;
        gfx::UploadTicket ticket;
    };
    struct MaterialRequest {
        MaterialAssetHandle handle;
        std::unique_ptr<MaterialAsset> staged;
        gfx::MaterialDesc desc;
        std::vector<std::pair<size_t, TextureHandle>> slots; // texture slot index, texture
    };

    std::unique_ptr<TextureAsset> createTexture(DecodedTexture& decoded, const std::filesystem::path& file,
                                                const TextureLoadSettings& settings, gfx::UploadBatch& batch);
    std::unique_ptr<ModelAsset> createModel(const DecodedModel& decoded, const std::filesystem::path& file,
                                            const ModelLoadSettings& settings, gfx::UploadBatch& batch);
    void submitDecode(uint64_t id, std::function<Decoded()> work);
    void processDecoded(uint64_t budget);
    void processUploads();
    void processMaterials();

    gfx::VulkanContext& m_context;
    gfx::UploadContext& m_upload;
    gfx::SceneRenderer& m_scene;
    AssetManagerDesc m_desc;
    AssetCache<TextureAsset> m_textures;
    AssetCache<ModelAsset> m_models;
    AssetCache<MaterialAsset> m_materials;

    uint64_t m_nextRequest = 1;
    std::shared_ptr<DecodeQueue> m_decodeQueue; // filled by decode jobs (any thread)
    std::map<uint64_t, TextureRequest> m_textureRequests; // decoding (by request id)
    std::map<uint64_t, ModelRequest> m_modelRequests;
    std::deque<std::unique_ptr<Decoded>> m_decoded;      // decoded, waiting for upload budget
    std::vector<TextureUpload> m_textureUploads;
    std::vector<ModelUpload> m_modelUploads;
    std::vector<MaterialRequest> m_materialRequests;
};

} // namespace zodar
