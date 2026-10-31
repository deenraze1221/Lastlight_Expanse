#include "Assets/TextureStandards.h"

namespace zodar {

TextureSizeClass classifyTextureSize(uint32_t width, uint32_t height) {
    if (width != height) {
        return TextureSizeClass::NonStandard;
    }
    switch (width) {
    case kTextureSizeSmall: return TextureSizeClass::Small;
    case kTextureSizePrimary: return TextureSizeClass::Primary;
    case kTextureSizeLarge: return TextureSizeClass::Large;
    case kTextureSizeUnique: return TextureSizeClass::Unique;
    default: return TextureSizeClass::NonStandard;
    }
}

const char* toString(TextureSizeClass sizeClass) {
    switch (sizeClass) {
    case TextureSizeClass::Small: return "small (32x32)";
    case TextureSizeClass::Primary: return "primary (64x64)";
    case TextureSizeClass::Large: return "large (128x128)";
    case TextureSizeClass::Unique: return "unique (256x256)";
    case TextureSizeClass::NonStandard: break;
    }
    return "non-standard";
}

std::optional<std::string> checkTextureSize(std::string_view name, uint32_t width, uint32_t height) {
    if (classifyTextureSize(width, height) != TextureSizeClass::NonStandard) {
        return std::nullopt;
    }
    return std::string(name) + ": " + std::to_string(width) + "x" + std::to_string(height) +
           " is not a standard texture size (32x32 small props, 64x64 primary, 128x128 characters/weapons, "
           "256x256 unique assets)";
}

} // namespace zodar
