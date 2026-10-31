#include "Assets/TextureStandards.h"

#include <bit>

namespace zodar {

TextureSizeClass classifyTextureSize(uint32_t width, uint32_t height) {
    if (width != height || !std::has_single_bit(width) || width < kTextureSizeMin) {
        return TextureSizeClass::NonStandard;
    }
    if (width < kTextureSizeDefault) return TextureSizeClass::Small;
    if (width == kTextureSizeDefault) return TextureSizeClass::Default;
    if (width == kTextureSizeMax) return TextureSizeClass::Maximum;
    return TextureSizeClass::Oversized;
}

const char* toString(TextureSizeClass sizeClass) {
    switch (sizeClass) {
    case TextureSizeClass::Small: return "small (below 32x32)";
    case TextureSizeClass::Default: return "default (32x32)";
    case TextureSizeClass::Maximum: return "maximum (64x64)";
    case TextureSizeClass::Oversized: return "oversized (above 64x64)";
    case TextureSizeClass::NonStandard: break;
    }
    return "non-standard";
}

bool isTextureSizeAllowed(TextureSizeClass sizeClass) {
    return sizeClass == TextureSizeClass::Small || sizeClass == TextureSizeClass::Default ||
           sizeClass == TextureSizeClass::Maximum;
}

std::optional<std::string> checkTextureSize(std::string_view name, uint32_t width, uint32_t height) {
    const TextureSizeClass c = classifyTextureSize(width, height);
    if (isTextureSizeAllowed(c)) {
        return std::nullopt;
    }
    const std::string size = std::to_string(width) + "x" + std::to_string(height);
    if (c == TextureSizeClass::Oversized) {
        return std::string(name) + ": " + size +
               " exceeds the 64x64 maximum texture size (default 32x32; not resized: re-author the texture)";
    }
    return std::string(name) + ": " + size +
           " is not a standard texture size (square power of two: 32x32 default, 64x64 maximum, 8/16 for tiny "
           "details)";
}

} // namespace zodar
