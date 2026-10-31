#include "AssetPipeline/ModelCompiler.h"

#include "Assets/Ktx2.h"
#include "Assets/ModelData.h"
#include "Core/Log.h"

#include <chrono>

namespace zodar::assets {

std::vector<CompiledTexture> compileModelTextures(const std::filesystem::path& model, const ModelCompileOptions& options) {
    GltfLoadOptions load;
    load.useCompiledTextures = false;
    load.generateMips = false;
    load.keepImageSources = true;
    const ModelData data = loadGltf(model, load);

    std::vector<CompiledTexture> results;
    for (size_t i = 0; i < data.images.size(); ++i) {
        const ModelImageSource& src = data.imageSources[i];
        const ImageData& image = data.images[i];
        const auto start = std::chrono::steady_clock::now();
        CompiledTexture r{src.cachePath, src.usage, PixelFormat::Bc7, image.width, image.height, 0,
                          src.bytes.size(), 0, 0, false, 0.0, 0.0};
        if (!options.force && std::filesystem::exists(src.cachePath)) {
            try {
                const Ktx2File existing = loadKtx2(src.cachePath);
                const auto it = existing.keyValues.find(kTextureSourceKey);
                if (it != existing.keyValues.end() && it->second == src.sourceKey) {
                    r.upToDate = true;
                    r.format = existing.image.format;
                    r.levels = static_cast<uint32_t>(existing.image.levels.size());
                    r.compiledBytes = existing.image.byteSize();
                    results.push_back(r);
                    continue;
                }
            } catch (const std::exception& e) {
                ZODAR_LOG_WARN("Recompiling unreadable '{}': {}", src.cachePath.string(), e.what());
            }
        }
        TextureCompileOptions texture = optionsFor(src.usage);
        texture.quality = options.quality;
        texture.threads = options.threads;
        const ImageData compiled = compileTexture(image, texture);
        auto keys = compilerKeyValues(texture);
        keys[kTextureSourceKey] = src.sourceKey;
        saveKtx2(src.cachePath, compiled, keys);

        ImageData reference = image;
        reference.levels.resize(1);
        reference.generateMips(texture.mipFilter);
        r.rgba8Bytes = reference.byteSize();
        r.levels = static_cast<uint32_t>(compiled.levels.size());
        r.compiledBytes = compiled.byteSize();
        r.format = compiled.format;
        ImageData level0 = compiled;
        level0.levels.resize(1);
        r.psnr = psnr(decompressToRgba8(level0).levels[0], image.levels[0], src.usage == TextureUsage::Normal ? 3 : 4);
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        results.push_back(r);
    }
    return results;
}

} // namespace zodar::assets
