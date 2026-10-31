#include "Assets/TextureCache.h"

#include <cstdio>

namespace zodar {

ColorSpace colorSpaceFor(TextureUsage usage) {
    return usage == TextureUsage::Color ? ColorSpace::Srgb : ColorSpace::Linear;
}

MipFilter mipFilterFor(TextureUsage usage) {
    return usage == TextureUsage::Normal ? MipFilter::NormalMap : MipFilter::Box;
}

const char* toString(TextureUsage usage) {
    switch (usage) {
    case TextureUsage::Color: return "color";
    case TextureUsage::Data: return "data";
    case TextureUsage::Normal: return "normal";
    }
    return "?";
}

std::filesystem::path textureCacheDirectory(const std::filesystem::path& model) {
    return model.parent_path() / (model.filename().string() + ".zodar");
}

std::filesystem::path textureCachePath(const std::filesystem::path& model, int imageIndex, TextureUsage usage) {
    return textureCacheDirectory(model) / ("image" + std::to_string(imageIndex) + "_" + toString(usage) + ".ktx2");
}

std::string textureSourceKey(std::span<const uint8_t> sourceBytes, TextureUsage usage) {
    uint64_t h = 1469598103934665603ull; // FNV-1a 64
    for (const uint8_t b : sourceBytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    char text[64];
    std::snprintf(text, sizeof(text), "v%u-%s-%zu-%016llx", kTextureCompilerVersion, toString(usage),
                  sourceBytes.size(), static_cast<unsigned long long>(h));
    return text;
}

} // namespace zodar
