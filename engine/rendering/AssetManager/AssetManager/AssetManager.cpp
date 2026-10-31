#include "AssetManager/AssetManager.h"

#include "AssetManager/AssetRegistry.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <variant>

namespace zodar {
namespace {

// Deletion-queue items must be copyable std::functions: share the unique ownership.
template <class T>
std::shared_ptr<T> share(std::unique_ptr<T> value) {
    return std::shared_ptr<T>(std::move(value));
}

void releaseModelMaterials(gfx::MaterialSystem& materials, const gfx::Model& model) {
    for (const gfx::MaterialHandle h : model.materials()) {
        materials.releaseMaterial(h);
    }
    materials.releaseMaterial(model.fallbackMaterial());
}

// Deferred destruction of replaced or released assets (frames in flight may still use them).
void retireTexture(gfx::VulkanContext& context, std::unique_ptr<TextureAsset> t) {
    context.deletionQueue().retire([t = share(std::move(t))]() mutable { t.reset(); });
}

void retireModel(gfx::VulkanContext& context, gfx::SceneRenderer& scene, std::unique_ptr<ModelAsset> m) {
    context.deletionQueue().retire([m = share(std::move(m)), &scene]() mutable {
        releaseModelMaterials(scene.materials(), m->model);
        m.reset();
    });
}

bool samePipelineState(const gfx::MaterialDesc& a, const gfx::MaterialDesc& b) {
    return a.shader == b.shader && a.alphaMode == b.alphaMode && a.doubleSided == b.doubleSided;
}

std::string samplerKey(const std::optional<gfx::SamplerDesc>& sampler) {
    return sampler ? std::to_string(gfx::SamplerDescHash{}(*sampler)) : "file";
}

std::string textureKey(const std::filesystem::path& file, const TextureLoadSettings& settings) {
    return file.generic_string() + "|" + toString(settings.usage) + (settings.generateMips ? "|mips" : "|nomips");
}

std::string modelKey(const std::filesystem::path& file, const ModelLoadSettings& settings) {
    const std::string shaders = std::to_string(settings.pbrShader.value_or(UINT32_MAX)) + "," +
                                std::to_string(settings.unlitShader.value_or(UINT32_MAX));
    return file.generic_string() + "|" + samplerKey(settings.samplerOverride) + "|" + shaders +
           (settings.useCompiledTextures ? "|ktx2" : "|src");
}

struct TextureSlot {
    MaterialTextureData MaterialData::*data;
    gfx::TextureBinding gfx::MaterialDesc::*binding;
    TextureUsage usage;
    const char* name;
};
constexpr TextureSlot kSlots[] = {
    {&MaterialData::baseColorTexture, &gfx::MaterialDesc::baseColor, TextureUsage::Color, "baseColorTexture"},
    {&MaterialData::metallicRoughnessTexture, &gfx::MaterialDesc::metallicRoughness, TextureUsage::Data,
     "metallicRoughnessTexture"},
    {&MaterialData::normalTexture, &gfx::MaterialDesc::normal, TextureUsage::Normal, "normalTexture"},
    {&MaterialData::occlusionTexture, &gfx::MaterialDesc::occlusion, TextureUsage::Data, "occlusionTexture"},
    {&MaterialData::emissiveTexture, &gfx::MaterialDesc::emissiveTexture, TextureUsage::Color, "emissiveTexture"},
};

std::filesystem::path texturePath(const MaterialTextureData& t, const std::filesystem::path& baseDirectory) {
    const std::filesystem::path p(t.path);
    return p.is_absolute() ? p : baseDirectory / p;
}

} // namespace

// CPU results of a decode (thread-safe to produce: no GPU or cache access).
struct AssetManager::DecodedTexture {
    ImageData image;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::string> warnings;
};

struct AssetManager::DecodedModel {
    ModelData data;
    std::vector<std::string> warnings;
};

struct AssetManager::Decoded {
    uint64_t id = 0;
    std::variant<std::monostate, DecodedTexture, DecodedModel> data;
    std::string error; // non-empty: decoding failed
};

// Hand-off from decode jobs (worker threads) to update() (render thread).
struct AssetManager::DecodeQueue {
    std::mutex mutex;
    std::vector<std::unique_ptr<Decoded>> done;
};

std::unordered_map<std::string, gfx::MaterialHandle> SceneMaterialAssets::handles() const {
    std::unordered_map<std::string, gfx::MaterialHandle> result;
    for (const auto& [id, material] : byId) {
        result[id] = material->handle;
    }
    return result;
}

std::unordered_map<std::string, const gfx::Model*> SceneModelAssets::models() const {
    std::unordered_map<std::string, const gfx::Model*> result;
    for (const auto& [reference, model] : byReference) {
        result[reference] = &model->model;
    }
    return result;
}

bool SceneModelAssets::loading() const {
    return std::ranges::any_of(byReference, [](const auto& entry) { return entry.second.loading(); });
}

std::vector<std::string> SceneModelAssets::errors() const {
    std::vector<std::string> result = unresolved;
    for (const auto& [reference, model] : byReference) {
        if (model.failed()) {
            result.push_back(reference + ": " + model.error());
        }
    }
    std::ranges::sort(result);
    return result;
}

bool SceneMaterialAssets::loading() const {
    return std::ranges::any_of(byId, [](const auto& entry) { return entry.second.loading(); });
}

std::vector<std::string> SceneMaterialAssets::errors() const {
    std::vector<std::string> result;
    for (const auto& [id, material] : byId) {
        if (material.failed()) {
            result.push_back(id + ": " + material.error());
        }
    }
    std::ranges::sort(result);
    return result;
}

AssetManager::AssetManager(gfx::VulkanContext& context, gfx::UploadContext& upload, gfx::SceneRenderer& scene,
                           AssetManagerDesc desc)
    : m_context(context), m_upload(upload), m_scene(scene), m_desc(std::move(desc)),
      m_textures([&context](std::unique_ptr<TextureAsset> t) { retireTexture(context, std::move(t)); }),
      m_models([&context, &scene](std::unique_ptr<ModelAsset> m) { retireModel(context, scene, std::move(m)); }),
      m_materials([&context, &scene](std::unique_ptr<MaterialAsset> m) {
          // Dropping the texture handles retires the textures in the same submission window.
          const gfx::MaterialHandle handle = m->handle;
          m.reset();
          context.deletionQueue().retire([handle, &scene] { scene.materials().releaseMaterial(handle); });
      }),
      m_decodeQueue(std::make_shared<DecodeQueue>()) {}

AssetManager::~AssetManager() {
    m_context.waitIdle();
    // Pending async loads: the device is idle, so staged GPU objects are destroyed directly.
    for (ModelUpload& u : m_modelUploads) {
        releaseModelMaterials(m_scene.materials(), u.staged->model);
    }
    m_materialRequests.clear();
    m_textureUploads.clear();
    m_modelUploads.clear();
    m_decoded.clear();
    m_textureRequests.clear();
    m_modelRequests.clear();
    m_context.deletionQueue().flushAll();
    const size_t live = m_textures.size() + m_models.size() + m_materials.size();
    if (live > 0) {
        ZODAR_LOG_ERROR("AssetManager destroyed with {} live assets (handles still held); they are destroyed when "
                        "released",
                        live);
    }
    // The device is idle: late releases destroy at once.
    gfx::SceneRenderer& scene = m_scene;
    m_textures.setRetire([](std::unique_ptr<TextureAsset>) {});
    m_models.setRetire([&scene](std::unique_ptr<ModelAsset> m) { releaseModelMaterials(scene.materials(), m->model); });
    m_materials.setRetire([&scene](std::unique_ptr<MaterialAsset> m) { scene.materials().releaseMaterial(m->handle); });
}

std::filesystem::path AssetManager::resolve(const std::filesystem::path& path) const {
    const std::filesystem::path full =
        path.is_absolute() || m_desc.root.empty() ? path : m_desc.root / path;
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(full, ec), ec);
    return ec ? full.lexically_normal() : canonical;
}

// --- GPU creation (render thread) -------------------------------------------------------

std::unique_ptr<TextureAsset> AssetManager::createTexture(DecodedTexture& decoded, const std::filesystem::path& file,
                                                          const TextureLoadSettings& settings, gfx::UploadBatch& batch) {
    auto asset = std::make_unique<TextureAsset>();
    asset->path = file;
    asset->usage = settings.usage;
    asset->width = decoded.width;
    asset->height = decoded.height;
    asset->sizeClass = classifyTextureSize(decoded.width, decoded.height);
    asset->warnings = std::move(decoded.warnings);
    asset->mipLevels = static_cast<uint32_t>(decoded.image.levels.size());
    asset->texture = gfx::Texture(batch, m_scene.bindless(), decoded.image);
    return asset;
}

std::unique_ptr<ModelAsset> AssetManager::createModel(const DecodedModel& decoded, const std::filesystem::path& file,
                                                      const ModelLoadSettings& settings, gfx::UploadBatch& batch) {
    gfx::ModelUploadOptions upload;
    upload.samplerOverride = settings.samplerOverride;
    upload.defaultSampler = m_desc.defaultSampler;
    upload.pbrShader = settings.pbrShader;
    upload.unlitShader = settings.unlitShader;
    return std::make_unique<ModelAsset>(ModelAsset{gfx::Model(batch, m_scene, decoded.data, upload), file, decoded.warnings});
}

// Decoders shared by blocking and async loads. Pure CPU work, safe on worker threads.
namespace {

template <class Decoded>
Decoded decodeTextureFile(const std::filesystem::path& file, const TextureLoadSettings& settings, bool checkSizes) {
    Decoded d;
    d.image = loadImage(file, colorSpaceFor(settings.usage));
    d.width = d.image.width;
    d.height = d.image.height;
    if (checkSizes) {
        if (auto warning = checkTextureSize(file.filename().string(), d.width, d.height)) {
            ZODAR_LOG_WARN("{}", *warning);
            d.warnings.push_back(std::move(*warning));
        }
    }
    if (settings.generateMips) {
        d.image.generateMips(mipFilterFor(settings.usage));
    }
    return d;
}

template <class Decoded>
Decoded decodeModelFile(const std::filesystem::path& file, const ModelLoadSettings& settings, bool checkSizes) {
    Decoded d;
    GltfLoadOptions loadOptions;
    loadOptions.useCompiledTextures = settings.useCompiledTextures;
    // A nearest-magnification override marks every texture as pixel art (no lossy caches).
    loadOptions.pixelArt = settings.samplerOverride && settings.samplerOverride->magFilter == gfx::Filter::Nearest;
    d.data = loadGltf(file, loadOptions);
    d.warnings = d.data.warnings;
    if (checkSizes) {
        for (const ImageData& image : d.data.images) {
            const std::string name = file.filename().string() + "/" + (image.name.empty() ? "image" : image.name);
            if (auto warning = checkTextureSize(name, image.width, image.height)) {
                d.warnings.push_back(std::move(*warning));
            }
        }
    }
    for (const std::string& w : d.warnings) {
        ZODAR_LOG_WARN("{}: {}", file.filename().string(), w);
    }
    return d;
}

uint64_t uploadBytes(const ModelData& data) {
    uint64_t bytes = 0;
    for (const ImageData& image : data.images) {
        bytes += image.byteSize();
    }
    for (const MeshData& mesh : data.meshes) {
        bytes += mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(uint32_t);
    }
    return bytes;
}

} // namespace

// --- Blocking loads ---------------------------------------------------------------------

TextureHandle AssetManager::loadTexture(const std::filesystem::path& path, const TextureLoadSettings& settings) {
    const std::filesystem::path file = resolve(path);
    const std::string key = textureKey(file, settings);
    m_textureSources.try_emplace(key, TextureSource{file, settings});
    TextureHandle handle = m_textures.getOrLoad(key, [&] {
        auto decoded = decodeTextureFile<DecodedTexture>(file, settings, m_desc.checkTextureSizes);
        gfx::UploadBatch batch(m_upload);
        auto asset = createTexture(decoded, file, settings, batch);
        batch.submit();
        return asset;
    });
    if (handle.loading()) {
        waitAll();
    }
    return handle;
}

ModelHandle AssetManager::loadModel(const std::filesystem::path& path, const ModelLoadSettings& settings) {
    const std::filesystem::path file = resolve(path);
    const std::string key = modelKey(file, settings);
    m_modelSources.try_emplace(key, ModelSource{file, settings, std::nullopt});
    ModelHandle handle = m_models.getOrLoad(key, [&] {
        const auto decoded = decodeModelFile<DecodedModel>(file, settings, m_desc.checkTextureSizes);
        gfx::UploadBatch batch(m_upload);
        auto asset = createModel(decoded, file, settings, batch);
        batch.submit();
        return asset;
    });
    if (handle.loading()) {
        waitAll();
    }
    return handle;
}

MaterialAssetHandle AssetManager::loadMaterial(const std::filesystem::path& path) {
    const std::filesystem::path file = resolve(path);
    const std::string key = file.generic_string();
    if (MaterialAssetHandle cached = m_materials.find(key); cached.valid()) {
        return createMaterial({}, {}, key); // counts the hit (and waits if loading); data unused
    }
    MaterialData data;
    try {
        data = loadMaterialFile(file);
    } catch (const std::exception& e) {
        const std::string message = e.what();
        return m_materials.getOrLoad(key, [&]() -> std::unique_ptr<MaterialAsset> { throw Error(message); });
    }
    return createMaterial(data, file.parent_path(), key);
}

MaterialAssetHandle AssetManager::createMaterial(const MaterialData& data, const std::filesystem::path& baseDirectory,
                                                 const std::string& key) {
    MaterialAssetHandle handle = m_materials.getOrLoad(key, [&] {
        PreparedMaterial prepared = prepareMaterial(data, baseDirectory);
        auto asset = std::make_unique<MaterialAsset>();
        asset->data = data;
        asset->path = key.substr(0, key.find('#'));
        asset->textures = std::move(prepared.textures);
        asset->handle = m_scene.materials().createMaterial(prepared.desc);
        return asset;
    });
    if (handle.loading()) {
        waitAll();
    }
    return handle;
}

AssetManager::PreparedMaterial AssetManager::prepareMaterial(const MaterialData& data,
                                                            const std::filesystem::path& baseDirectory) {
    PreparedMaterial prepared;
    prepared.desc = toMaterialDesc(data, 0);
    try {
        prepared.desc.shader = m_scene.materials().findShader(data.shader);
    } catch (const Error&) {
        fail("material '" + data.name + "': unknown shader '" + data.shader + "'");
    }
    for (const TextureSlot& slot : kSlots) {
        const MaterialTextureData& t = data.*slot.data;
        if (t.empty()) {
            continue;
        }
        TextureHandle texture = loadTexture(texturePath(t, baseDirectory), {slot.usage, true});
        if (texture.failed()) {
            fail("material '" + data.name + "': " + slot.name + ": " + texture.error());
        }
        gfx::TextureBinding& binding = prepared.desc.*slot.binding;
        binding.texture = &texture->texture;
        binding.sampler = samplerFor(t, m_desc.defaultSampler);
        prepared.textures.push_back(std::move(texture));
    }
    return prepared;
}

SceneMaterialAssets AssetManager::createSceneMaterials(const SceneDocument& document,
                                                       const std::filesystem::path& sceneFile) {
    const std::filesystem::path scene = resolve(sceneFile);
    const std::filesystem::path directory = scene.parent_path();
    SceneMaterialAssets result;
    std::vector<DataIssue> issues;
    for (const SceneMaterial& material : document.materials) {
        MaterialAssetHandle handle =
            material.source.empty()
                ? createMaterial(material.data, sceneMaterialDirectory(material, directory),
                                 scene.generic_string() + "#" + material.id)
                : loadMaterial(directory / material.source);
        if (handle.failed()) {
            issues.push_back({scene.generic_string(), 0, 0, "/materials/" + material.id, handle.error()});
        }
        result.byId[material.id] = std::move(handle);
    }
    if (!issues.empty()) {
        throw DataError(std::move(issues));
    }
    return result;
}

// --- Scene models (C4) --------------------------------------------------------------------

namespace {

template <class Load>
SceneModelAssets sceneModels(const SceneDocument& document, const std::filesystem::path& directory,
                             const AssetRegistry* registry, Load&& load) {
    SceneModelAssets result;
    for (const SceneEntity& entity : document.entities) {
        if (!entity.modelRenderer) {
            continue;
        }
        const std::string& reference = entity.modelRenderer->model;
        if (result.byReference.contains(reference) ||
            std::ranges::any_of(result.unresolved, [&](const std::string& u) { return u.starts_with(reference + ": "); })) {
            continue;
        }
        if (isModelFileReference(reference)) {
            result.byReference[reference] = load(directory / reference, ModelLoadSettings{});
            continue;
        }
        const AssetEntry* entry = registry ? registry->find(reference) : nullptr;
        if (!entry) {
            result.unresolved.push_back(reference + ": " +
                                        (registry ? "unknown asset id (not in " +
                                                        (registry->directory() / AssetRegistry::kFileName).generic_string() + ")"
                                                  : "not a model file (.gltf/.glb) and no asset registry given"));
        } else if (entry->type != AssetType::Model) {
            result.unresolved.push_back(reference + ": asset '" + reference + "' is a " + toString(entry->type) +
                                        ", not a model");
        } else {
            result.byReference[reference] = load(registry->sourcePath(*entry), modelLoadSettings(*entry));
        }
    }
    return result;
}

} // namespace

SceneModelAssets AssetManager::loadSceneModels(const SceneDocument& document, const std::filesystem::path& sceneFile,
                                               const AssetRegistry* registry) {
    const std::filesystem::path scene = resolve(sceneFile);
    SceneModelAssets result = sceneModels(document, scene.parent_path(), registry,
                                          [&](const std::filesystem::path& p, const ModelLoadSettings& s) {
                                              return loadModel(p, s);
                                          });
    if (const std::vector<std::string> errors = result.errors(); !errors.empty()) {
        std::vector<DataIssue> issues;
        for (const std::string& e : errors) {
            issues.push_back({scene.generic_string(), 0, 0, "", "modelRenderer " + e});
        }
        throw DataError(std::move(issues));
    }
    return result;
}

SceneModelAssets AssetManager::loadSceneModelsAsync(const SceneDocument& document,
                                                    const std::filesystem::path& sceneFile,
                                                    const AssetRegistry* registry) {
    const std::filesystem::path scene = resolve(sceneFile);
    return sceneModels(document, scene.parent_path(), registry,
                       [&](const std::filesystem::path& p, const ModelLoadSettings& s) { return loadModelAsync(p, s); });
}

// --- Async loads ------------------------------------------------------------------------

void AssetManager::submitDecode(uint64_t id, std::function<Decoded()> work) {
    // The job only touches its captured inputs and the shared queue (never handles/caches).
    auto job = [id, work = std::move(work), queue = m_decodeQueue] {
        auto result = std::make_unique<Decoded>();
        try {
            *result = work();
        } catch (const std::exception& e) {
            result->data = std::monostate{};
            result->error = e.what();
            if (result->error.empty()) {
                result->error = "decode failed";
            }
        }
        result->id = id;
        std::lock_guard lock(queue->mutex);
        queue->done.push_back(std::move(result));
    };
    if (m_desc.jobs) {
        m_desc.jobs->submit(std::move(job));
    } else {
        job();
    }
}

TextureHandle AssetManager::loadTextureAsync(const std::filesystem::path& path, const TextureLoadSettings& settings) {
    const std::filesystem::path file = resolve(path);
    bool created = false;
    const std::string key = textureKey(file, settings);
    m_textureSources.try_emplace(key, TextureSource{file, settings});
    TextureHandle handle = m_textures.getOrCreatePending(key, created);
    if (!created) {
        return handle;
    }
    const uint64_t id = m_nextRequest++;
    m_textureRequests[id] = {handle, settings, file};
    const bool checkSizes = m_desc.checkTextureSizes;
    submitDecode(id, [file, settings, checkSizes] {
        Decoded d;
        d.data = decodeTextureFile<DecodedTexture>(file, settings, checkSizes);
        return d;
    });
    return handle;
}

ModelHandle AssetManager::loadModelAsync(const std::filesystem::path& path, const ModelLoadSettings& settings) {
    const std::filesystem::path file = resolve(path);
    bool created = false;
    const std::string key = modelKey(file, settings);
    m_modelSources.try_emplace(key, ModelSource{file, settings, std::nullopt});
    ModelHandle handle = m_models.getOrCreatePending(key, created);
    if (!created) {
        return handle;
    }
    const uint64_t id = m_nextRequest++;
    m_modelRequests[id] = {handle, settings, file};
    const bool checkSizes = m_desc.checkTextureSizes;
    submitDecode(id, [file, settings, checkSizes] {
        Decoded d;
        d.data = decodeModelFile<DecodedModel>(file, settings, checkSizes);
        return d;
    });
    return handle;
}

MaterialAssetHandle AssetManager::loadMaterialAsync(const std::filesystem::path& path) {
    const std::filesystem::path file = resolve(path);
    const std::string key = file.generic_string();
    if (MaterialAssetHandle cached = m_materials.find(key); cached.valid()) {
        bool created = false;
        return m_materials.getOrCreatePending(key, created); // counts the hit
    }
    MaterialData data;
    try {
        data = loadMaterialFile(file);
    } catch (const std::exception& e) {
        bool created = false;
        MaterialAssetHandle handle = m_materials.getOrCreatePending(key, created);
        m_materials.reject(handle, e.what());
        return handle;
    }
    return createMaterialAsync(data, file.parent_path(), key);
}

MaterialAssetHandle AssetManager::createMaterialAsync(const MaterialData& data,
                                                      const std::filesystem::path& baseDirectory,
                                                      const std::string& key) {
    bool created = false;
    MaterialAssetHandle handle = m_materials.getOrCreatePending(key, created);
    if (!created) {
        return handle;
    }
    MaterialRequest request;
    request.handle = handle;
    request.staged = std::make_unique<MaterialAsset>();
    request.staged->data = data;
    request.staged->path = key.substr(0, key.find('#'));
    request.desc = toMaterialDesc(data, 0);
    try {
        request.desc.shader = m_scene.materials().findShader(data.shader);
    } catch (const Error&) {
        m_materials.reject(handle, "material '" + data.name + "': unknown shader '" + data.shader + "'");
        return handle;
    }
    for (size_t i = 0; i < std::size(kSlots); ++i) {
        const MaterialTextureData& t = data.*kSlots[i].data;
        if (t.empty()) {
            continue;
        }
        (request.desc.*kSlots[i].binding).sampler = samplerFor(t, m_desc.defaultSampler);
        request.slots.emplace_back(i, loadTextureAsync(texturePath(t, baseDirectory), {kSlots[i].usage, true}));
    }
    m_materialRequests.push_back(std::move(request));
    processMaterials(); // textures that are cached and ready complete the material at once
    return handle;
}

SceneMaterialAssets AssetManager::createSceneMaterialsAsync(const SceneDocument& document,
                                                            const std::filesystem::path& sceneFile) {
    const std::filesystem::path scene = resolve(sceneFile);
    const std::filesystem::path directory = scene.parent_path();
    SceneMaterialAssets result;
    for (const SceneMaterial& material : document.materials) {
        result.byId[material.id] =
            material.source.empty()
                ? createMaterialAsync(material.data, sceneMaterialDirectory(material, directory),
                                      scene.generic_string() + "#" + material.id)
                : loadMaterialAsync(directory / material.source);
    }
    return result;
}

void AssetManager::processDecoded(uint64_t budget) {
    {
        std::lock_guard lock(m_decodeQueue->mutex);
        for (auto& d : m_decodeQueue->done) {
            m_decoded.push_back(std::move(d));
        }
        m_decodeQueue->done.clear();
    }
    // Request order, so uploads start in a stable order whatever the worker timing.
    std::ranges::sort(m_decoded, {}, [](const std::unique_ptr<Decoded>& d) { return d->id; });

    uint64_t staged = 0;
    bool started = false;
    while (!m_decoded.empty()) {
        Decoded& d = *m_decoded.front();
        if (auto it = m_textureRequests.find(d.id); it != m_textureRequests.end()) {
            TextureRequest& r = it->second;
            if (r.handle.useCount() == 1) { // every user handle dropped: cancel
                m_textures.reject(r.handle, "load canceled");
            } else if (!d.error.empty()) {
                ZODAR_LOG_WARN("{}", d.error);
                m_textures.reject(r.handle, d.error);
            } else {
                auto& decoded = std::get<DecodedTexture>(d.data);
                const uint64_t bytes = decoded.image.byteSize();
                if (started && staged + bytes > budget) {
                    break; // next update
                }
                gfx::UploadBatch batch(m_upload);
                auto asset = createTexture(decoded, r.file, r.settings, batch);
                m_textureUploads.push_back({r.handle, std::move(asset), batch.submitAsync()});
                staged += bytes;
                started = true;
            }
            m_textureRequests.erase(it);
        } else if (auto mt = m_modelRequests.find(d.id); mt != m_modelRequests.end()) {
            ModelRequest& r = mt->second;
            if (r.handle.useCount() == 1) {
                m_models.reject(r.handle, "load canceled");
            } else if (!d.error.empty()) {
                ZODAR_LOG_WARN("{}", d.error);
                m_models.reject(r.handle, d.error);
            } else {
                const auto& decoded = std::get<DecodedModel>(d.data);
                const uint64_t bytes = uploadBytes(decoded.data);
                if (started && staged + bytes > budget) {
                    break;
                }
                gfx::UploadBatch batch(m_upload);
                auto asset = createModel(decoded, r.file, r.settings, batch);
                m_modelUploads.push_back({r.handle, std::move(asset), batch.submitAsync()});
                staged += bytes;
                started = true;
            }
            m_modelRequests.erase(mt);
        }
        m_decoded.pop_front();
    }
}

void AssetManager::processUploads() {
    const gfx::UploadQueue& queue = m_upload.queue();
    // Resolving hands the asset to its handles; if every user handle was dropped meanwhile,
    // erasing the entry drops the last one and the asset is retired like a released asset.
    std::erase_if(m_textureUploads, [&](TextureUpload& u) {
        if (!queue.isReady(u.ticket)) {
            return false;
        }
        m_textures.resolve(u.handle, std::move(u.staged));
        return true;
    });
    std::erase_if(m_modelUploads, [&](ModelUpload& u) {
        if (!queue.isReady(u.ticket)) {
            return false;
        }
        m_models.resolve(u.handle, std::move(u.staged));
        return true;
    });
}

void AssetManager::processMaterials() {
    std::erase_if(m_materialRequests, [&](MaterialRequest& r) {
        std::string error;
        for (const auto& [slot, texture] : r.slots) {
            if (texture.loading()) {
                return false;
            }
            if (texture.failed() && error.empty()) {
                error = "material '" + r.staged->data.name + "': " + kSlots[slot].name + ": " + texture.error();
            }
        }
        if (!error.empty()) {
            m_materials.reject(r.handle, error);
            return true;
        }
        for (auto& [slot, texture] : r.slots) {
            (r.desc.*kSlots[slot].binding).texture = &texture->texture;
            r.staged->textures.push_back(std::move(texture));
        }
        r.staged->handle = m_scene.materials().createMaterial(r.desc);
        m_materials.resolve(r.handle, std::move(r.staged));
        return true;
    });
}

void AssetManager::update() {
    if (watching() && m_watcher.due(m_desc.watchInterval)) {
        pollFileChanges();
    }
    processDecoded(m_desc.uploadBudgetBytes);
    processUploads();
    processMaterials();
}

void AssetManager::waitAll() {
    while (pendingCount() > 0) {
        if (m_desc.jobs) {
            m_desc.jobs->waitIdle();
        }
        processDecoded(std::numeric_limits<uint64_t>::max());
        for (const TextureUpload& u : m_textureUploads) {
            m_upload.queue().wait(u.ticket);
        }
        for (const ModelUpload& u : m_modelUploads) {
            m_upload.queue().wait(u.ticket);
        }
        processUploads();
        processMaterials();
    }
}

size_t AssetManager::pendingCount() const {
    return m_textureRequests.size() + m_modelRequests.size() + m_textureUploads.size() + m_modelUploads.size() +
           m_materialRequests.size();
}

// --- Hot reload (C5) ----------------------------------------------------------------------

void AssetReloadResult::append(const AssetReloadResult& other) {
    reloaded.insert(reloaded.end(), other.reloaded.begin(), other.reloaded.end());
    failed.insert(failed.end(), other.failed.begin(), other.failed.end());
    materialsRefreshed += other.materialsRefreshed;
}

void AssetManager::commitMaterial(const MaterialAssetHandle& handle, const MaterialData& data,
                                  PreparedMaterial prepared) {
    gfx::MaterialSystem& materials = m_scene.materials();
    auto fresh = std::make_unique<MaterialAsset>();
    fresh->data = data;
    fresh->path = handle.key().substr(0, handle.key().find('#'));
    fresh->textures = std::move(prepared.textures);
    if (handle.ready()) {
        // Same gfx material: the GPU table entry is rewritten by the next recorded frame.
        fresh->handle = handle->handle;
        materials.updateMaterial(fresh->handle, prepared.desc);
    } else {
        fresh->handle = materials.createMaterial(prepared.desc);
    }
    // The previous content only holds texture handles now: dropping them retires textures
    // no longer used through the deletion queue (after the frames that may still read them).
    m_materials.replace(handle, std::move(fresh));
}

void AssetManager::reloadTexture(const TextureHandle& texture, AssetReloadResult& result) {
    const auto source = m_textureSources.find(texture.key());
    if (texture.loading() || source == m_textureSources.end()) {
        return;
    }
    const TextureSource& src = source->second;
    std::unique_ptr<TextureAsset> fresh;
    try {
        auto decoded = decodeTextureFile<DecodedTexture>(src.file, src.settings, m_desc.checkTextureSizes);
        gfx::UploadBatch batch(m_upload);
        fresh = createTexture(decoded, src.file, src.settings, batch);
        batch.submit();
    } catch (const std::exception& e) {
        result.failed.push_back(texture.key() + ": " + e.what());
        return;
    }
    if (std::unique_ptr<TextureAsset> old = m_textures.replace(texture, std::move(fresh))) {
        retireTexture(m_context, std::move(old));
    }
    result.reloaded.push_back(texture.key());
    // Materials store the texture's address; rewrite their GPU entries with the new slot.
    gfx::MaterialSystem& materials = m_scene.materials();
    for (const std::string& key : m_materials.keys()) {
        const MaterialAssetHandle material = m_materials.find(key);
        if (!material.ready() || std::ranges::find(material->textures, texture) == material->textures.end()) {
            continue;
        }
        const gfx::MaterialDesc desc = materials.material(material->handle);
        materials.updateMaterial(material->handle, desc);
        ++result.materialsRefreshed;
    }
}

void AssetManager::reloadModel(const ModelHandle& model, AssetReloadResult& result) {
    const auto source = m_modelSources.find(model.key());
    if (model.loading() || source == m_modelSources.end()) {
        return;
    }
    ModelSource& src = source->second;
    src.files.reset(); // dependencies may change with the file
    std::unique_ptr<ModelAsset> fresh;
    try {
        const auto decoded = decodeModelFile<DecodedModel>(src.file, src.settings, m_desc.checkTextureSizes);
        gfx::UploadBatch batch(m_upload);
        fresh = createModel(decoded, src.file, src.settings, batch);
        batch.submit();
    } catch (const std::exception& e) {
        result.failed.push_back(model.key() + ": " + e.what());
        return;
    }
    if (std::unique_ptr<ModelAsset> old = m_models.replace(model, std::move(fresh))) {
        retireModel(m_context, m_scene, std::move(old));
    }
    result.reloaded.push_back(model.key());
}

void AssetManager::reloadMaterialFile(const MaterialAssetHandle& material, AssetReloadResult& result,
                                      bool reportFailure) {
    if (material.loading()) {
        return;
    }
    const std::string& key = material.key();
    try {
        if (key.find('#') != std::string::npos) {
            fail("created from data, not from a material file (use updateMaterialData)");
        }
        const std::filesystem::path file(key);
        const MaterialData data = loadMaterialFile(file);
        PreparedMaterial prepared = prepareMaterial(data, file.parent_path());
        if (material.ready() &&
            !samePipelineState(m_scene.materials().material(material->handle), prepared.desc)) {
            fail("shader, alphaMode or doubleSided changed; hot reload cannot change pipeline state (restart)");
        }
        commitMaterial(material, data, std::move(prepared));
    } catch (const std::exception& e) {
        if (reportFailure) {
            result.failed.push_back(key + ": " + e.what());
        }
        return;
    }
    result.reloaded.push_back(key);
}

void AssetManager::reloadShaders(const std::set<std::filesystem::path>& changed, AssetReloadResult& result) {
    gfx::MaterialSystem& materials = m_scene.materials();
    for (gfx::ShaderHandle h = 0; h < materials.shaderCount(); ++h) {
        if (!changed.contains(materials.vertexFile(h)) && !changed.contains(materials.fragmentFile(h))) {
            continue;
        }
        const gfx::ShaderReloadResult r = materials.reloadShader(h, m_context.deletionQueue());
        const std::string key = "shader:" + materials.shaderName(h);
        if (r.ok) {
            result.reloaded.push_back(key);
        } else {
            result.failed.push_back(key + ": " + r.error);
        }
    }
}

AssetReloadResult AssetManager::reloadShader(const std::string& name) {
    AssetReloadResult result;
    gfx::MaterialSystem& materials = m_scene.materials();
    try {
        const gfx::ShaderHandle h = materials.findShader(name);
        reloadShaders({materials.vertexFile(h), materials.fragmentFile(h)}, result);
    } catch (const std::exception& e) {
        result.failed.push_back("shader:" + name + ": " + e.what());
    }
    finishReload(result);
    return result;
}

void AssetManager::finishReload(const AssetReloadResult& result) {
    m_reloads += result.reloaded.size();
    m_reloadFailures += result.failed.size();
    for (const std::string& key : result.reloaded) {
        ZODAR_LOG_INFO("Hot reload: reloaded {}", key);
    }
    for (const std::string& failure : result.failed) {
        ZODAR_LOG_WARN("Hot reload failed (previous version kept): {}", failure);
    }
}

AssetReloadResult AssetManager::reload(const TextureHandle& texture) {
    AssetReloadResult result;
    reloadTexture(texture, result);
    finishReload(result);
    return result;
}

AssetReloadResult AssetManager::reload(const ModelHandle& model) {
    AssetReloadResult result;
    reloadModel(model, result);
    finishReload(result);
    return result;
}

AssetReloadResult AssetManager::reload(const MaterialAssetHandle& material) {
    AssetReloadResult result;
    reloadMaterialFile(material, result, true);
    finishReload(result);
    return result;
}

AssetReloadResult AssetManager::updateMaterialData(const std::vector<MaterialDataUpdate>& updates) {
    AssetReloadResult result;
    std::vector<PreparedMaterial> prepared;
    prepared.reserve(updates.size());
    for (const MaterialDataUpdate& u : updates) {
        try {
            if (!u.handle.valid() || u.handle.loading()) {
                fail("material is empty or still loading");
            }
            prepared.push_back(prepareMaterial(u.data, u.baseDirectory));
            if (u.handle.ready() &&
                !samePipelineState(m_scene.materials().material(u.handle->handle), prepared.back().desc)) {
                fail("shader, alphaMode or doubleSided changed; hot reload cannot change pipeline state (restart)");
            }
        } catch (const std::exception& e) {
            result.failed.push_back((u.handle.valid() ? u.handle.key() : std::string("<empty>")) + ": " + e.what());
        }
    }
    if (result.failed.empty()) {
        for (size_t i = 0; i < updates.size(); ++i) {
            commitMaterial(updates[i].handle, updates[i].data, std::move(prepared[i]));
            result.reloaded.push_back(updates[i].handle.key());
        }
    }
    finishReload(result);
    return result;
}

std::vector<std::filesystem::path> AssetManager::watchedFiles() {
    std::erase_if(m_textureSources, [&](const auto& e) { return !m_textures.find(e.first).valid(); });
    std::erase_if(m_modelSources, [&](const auto& e) { return !m_models.find(e.first).valid(); });
    std::set<std::filesystem::path> files;
    for (const auto& [key, source] : m_textureSources) {
        files.insert(source.file);
    }
    for (auto& [key, source] : m_modelSources) {
        if (!source.files) {
            source.files = std::vector<std::filesystem::path>{source.file};
            try {
                const AssetFingerprint fingerprint = fingerprintAsset(AssetType::Model, source.file);
                source.files->insert(source.files->end(), fingerprint.dependencies.begin(),
                                     fingerprint.dependencies.end());
            } catch (const std::exception&) {
                // Unreadable model: watch the file itself; a fixed file reloads it.
            }
        }
        files.insert(source.files->begin(), source.files->end());
    }
    for (const std::string& key : m_materials.keys()) {
        if (key.find('#') == std::string::npos) {
            files.insert(std::filesystem::path(key));
        }
    }
    const gfx::MaterialSystem& materials = m_scene.materials();
    for (gfx::ShaderHandle h = 0; h < materials.shaderCount(); ++h) {
        files.insert(materials.vertexFile(h));
        files.insert(materials.fragmentFile(h));
    }
    return {files.begin(), files.end()};
}

AssetReloadResult AssetManager::reloadFiles(const std::vector<std::filesystem::path>& changedFiles) {
    AssetReloadResult result;
    std::set<std::filesystem::path> changed;
    for (const std::filesystem::path& p : changedFiles) {
        changed.insert(resolve(p));
    }
    if (changed.empty()) {
        return result;
    }
    watchedFiles(); // prunes released sources, fills model dependencies
    for (const auto& [key, source] : std::unordered_map(m_textureSources)) {
        if (changed.contains(source.file)) {
            reloadTexture(m_textures.find(key), result);
        }
    }
    for (const std::string& key : m_materials.keys()) {
        const MaterialAssetHandle material = m_materials.find(key);
        if (key.find('#') != std::string::npos || !material.valid()) {
            continue;
        }
        if (changed.contains(std::filesystem::path(key))) {
            reloadMaterialFile(material, result, true);
        } else if (material.failed()) {
            reloadMaterialFile(material, result, false); // retry quietly (a texture may be fixed)
        }
    }
    for (const auto& [key, source] : std::unordered_map(m_modelSources)) {
        if (std::ranges::any_of(*source.files, [&](const std::filesystem::path& f) { return changed.contains(f); })) {
            reloadModel(m_models.find(key), result);
        }
    }
    reloadShaders(changed, result);
    finishReload(result);
    return result;
}

AssetReloadResult AssetManager::pollFileChanges() {
    if (!watching()) {
        return {};
    }
    m_watcher.setWatched(watchedFiles());
    const std::vector<std::filesystem::path> changed = m_watcher.poll();
    return changed.empty() ? AssetReloadResult{} : reloadFiles(changed);
}

AssetManager::Stats AssetManager::stats() const {
    const auto& t = m_textures.stats();
    const auto& m = m_models.stats();
    const auto& a = m_materials.stats();
    Stats s;
    s.textures = m_textures.size();
    s.models = m_models.size();
    s.materials = m_materials.size();
    s.loads = t.loads + m.loads + a.loads;
    s.hits = t.hits + m.hits + a.hits;
    s.failures = t.failures + m.failures + a.failures;
    s.released = t.released + m.released + a.released;
    s.pending = pendingCount();
    s.uploading = m_textureUploads.size() + m_modelUploads.size();
    s.reloads = m_reloads;
    s.reloadFailures = m_reloadFailures;
    return s;
}

} // namespace zodar
