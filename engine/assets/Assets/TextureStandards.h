#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Texture size standards (Phase C, docs/ART_DIRECTION.md §2). Pixel-art textures are square
// powers of two in one of four size classes. Anything else is "non-standard": loading still
// works, but the asset manager and asset tools report a warning (never an error).
namespace zodar {

enum class TextureSizeClass : uint8_t {
    Small,       // 32×32: small props, items, decals
    Primary,     // 64×64: standard (terrain blocks, environment objects, props)
    Large,       // 128×128: characters, weapons
    Unique,      // 256×256: unique assets (maximum, give the reason in the asset notes)
    NonStandard, // not square, not a power of two, or outside 32..256
};

inline constexpr uint32_t kTextureSizeSmall = 32;
inline constexpr uint32_t kTextureSizePrimary = 64;
inline constexpr uint32_t kTextureSizeLarge = 128;
inline constexpr uint32_t kTextureSizeUnique = 256;

TextureSizeClass classifyTextureSize(uint32_t width, uint32_t height);
const char* toString(TextureSizeClass sizeClass);

// nullopt for a standard size; otherwise a warning naming the texture, its size and the
// allowed sizes ("<name>: 100x64 is not a standard texture size (...)").
std::optional<std::string> checkTextureSize(std::string_view name, uint32_t width, uint32_t height);

} // namespace zodar
