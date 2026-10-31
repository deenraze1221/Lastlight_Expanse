#include "AssetManager/AssetManager.h"

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
      m_textures([&context](std::unique_ptr<TextureAsset> t) {
          context.deletionQueue().retire([t = share(std::move(t))]() mutable { t.reset(); });
      }),
      m_models([&context, &scene](std::unique_ptr<ModelAsset> m) {
          context.deletionQueue().retire([m = share(std::move(m)), &scene]() mutable {
              releaseModelMaterials(scene.materials(), m->model);
              m.reset();
          });
      }),
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
    TextureHandle handle = m_textures.getOrLoad(textureKey(file, settings), [&] {
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
    ModelHandle handle = m_models.getOrLoad(modelKey(file, settings), [&] {
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
        gfx::MaterialSystem& materials = m_scene.materials();
        auto asset = std::make_unique<MaterialAsset>();
        asset->data = data;
        asset->path = key.substr(0, key.find('#'));
        gfx::MaterialDesc desc = toMaterialDesc(data, 0);
        try {
            desc.shader = materials.findShader(data.shader);
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
            gfx::TextureBinding& binding = desc.*slot.binding;
            binding.texture = &texture->texture;
            binding.sampler = samplerFor(t, m_desc.defaultSampler);
            asset->textures.push_back(std::move(texture));
        }
        asset->handle = materials.createMaterial(desc);
        return asset;
    });
    if (handle.loading()) {
        waitAll();
    }
    return handle;
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
                ? createMaterial(material.data, directory, scene.generic_string() + "#" + material.id)
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
    TextureHandle handle = m_textures.getOrCreatePending(textureKey(file, settings), created);
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
    ModelHandle handle = m_models.getOrCreatePending(modelKey(file, settings), created);
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
                ? createMaterialAsync(material.data, directory, scene.generic_string() + "#" + material.id)
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
    return s;
}

} // namespace zodar
