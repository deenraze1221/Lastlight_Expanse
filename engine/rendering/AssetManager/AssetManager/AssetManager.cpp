#include "AssetManager/AssetManager.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <memory>
#include <system_error>

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

} // namespace

std::unordered_map<std::string, gfx::MaterialHandle> SceneMaterialAssets::handles() const {
    std::unordered_map<std::string, gfx::MaterialHandle> result;
    for (const auto& [id, material] : byId) {
        result[id] = material->handle;
    }
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
      }) {}

AssetManager::~AssetManager() {
    m_context.waitIdle();
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

TextureHandle AssetManager::loadTexture(const std::filesystem::path& path, const TextureLoadSettings& settings) {
    const std::filesystem::path file = resolve(path);
    const std::string key =
        file.generic_string() + "|" + toString(settings.usage) + (settings.generateMips ? "|mips" : "|nomips");
    return m_textures.getOrLoad(key, [&] {
        ImageData image = loadImage(file, colorSpaceFor(settings.usage));
        auto asset = std::make_unique<TextureAsset>();
        asset->path = file;
        asset->usage = settings.usage;
        asset->width = image.width;
        asset->height = image.height;
        asset->sizeClass = classifyTextureSize(image.width, image.height);
        if (m_desc.checkTextureSizes) {
            if (auto warning = checkTextureSize(file.filename().string(), image.width, image.height)) {
                ZODAR_LOG_WARN("{}", *warning);
                asset->warnings.push_back(std::move(*warning));
            }
        }
        if (settings.generateMips) {
            image.generateMips(mipFilterFor(settings.usage));
        }
        asset->mipLevels = static_cast<uint32_t>(image.levels.size());
        gfx::UploadBatch batch(m_upload);
        asset->texture = gfx::Texture(batch, m_scene.bindless(), image);
        batch.submit();
        return asset;
    });
}

ModelHandle AssetManager::loadModel(const std::filesystem::path& path, const ModelLoadSettings& settings) {
    const std::filesystem::path file = resolve(path);
    const std::string shaders = std::to_string(settings.pbrShader.value_or(UINT32_MAX)) + "," +
                                std::to_string(settings.unlitShader.value_or(UINT32_MAX));
    const std::string key = file.generic_string() + "|" + samplerKey(settings.samplerOverride) + "|" + shaders +
                            (settings.useCompiledTextures ? "|ktx2" : "|src");
    return m_models.getOrLoad(key, [&] {
        GltfLoadOptions loadOptions;
        loadOptions.useCompiledTextures = settings.useCompiledTextures;
        const ModelData data = loadGltf(file, loadOptions);
        std::vector<std::string> warnings = data.warnings;
        if (m_desc.checkTextureSizes) {
            for (const ImageData& image : data.images) {
                const std::string name = file.filename().string() + "/" + (image.name.empty() ? "image" : image.name);
                if (auto warning = checkTextureSize(name, image.width, image.height)) {
                    warnings.push_back(std::move(*warning));
                }
            }
        }
        for (const std::string& w : warnings) {
            ZODAR_LOG_WARN("{}: {}", file.filename().string(), w);
        }
        gfx::ModelUploadOptions upload;
        upload.samplerOverride = settings.samplerOverride;
        upload.defaultSampler = m_desc.defaultSampler;
        upload.pbrShader = settings.pbrShader;
        upload.unlitShader = settings.unlitShader;
        gfx::UploadBatch batch(m_upload);
        auto asset = std::make_unique<ModelAsset>(
            ModelAsset{gfx::Model(batch, m_scene, data, upload), file, std::move(warnings)});
        batch.submit();
        return asset;
    });
}

MaterialAssetHandle AssetManager::loadMaterial(const std::filesystem::path& path) {
    const std::filesystem::path file = resolve(path);
    const std::string key = file.generic_string();
    if (MaterialAssetHandle cached = m_materials.find(key); cached.valid()) {
        return createMaterial({}, {}, key); // counts the hit; data unused for cached keys
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
    return m_materials.getOrLoad(key, [&] {
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
            const std::filesystem::path texturePath =
                std::filesystem::path(t.path).is_absolute() ? std::filesystem::path(t.path) : baseDirectory / t.path;
            TextureHandle texture = loadTexture(texturePath, {slot.usage, true});
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
    return s;
}

} // namespace zodar
