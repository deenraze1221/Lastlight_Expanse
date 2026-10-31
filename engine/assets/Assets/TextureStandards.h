#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Texture size standards (docs/ART_DIRECTION.md §2). Pixel-art textures are square powers of
// two: 32×32 is the default for every asset, 64×64 the absolute maximum (characters, weapons
// and unique assets included). Smaller power-of-two squares (8, 16) are allowed for tiny
// details. Anything else is a violation: it still loads (textures are never resized), but
// the asset manager, asset checks and tools report a warning.
namespace zodar {

enum class TextureSizeClass : uint8_t {
    Small,       // 8×8 or 16×16: tiny details (allowed)
    Default,     // 32×32: the default for all assets
    Maximum,     // 64×64: absolute maximum
    Oversized,   // square power of two above 64×64: violation
    NonStandard, // not square, not a power of two, or below 8: violation
};

inline constexpr uint32_t kTextureSizeMin = 8;
inline constexpr uint32_t kTextureSizeDefault = 32;
inline constexpr uint32_t kTextureSizeMax = 64;

TextureSizeClass classifyTextureSize(uint32_t width, uint32_t height);
const char* toString(TextureSizeClass sizeClass);
// Small, Default or Maximum.
bool isTextureSizeAllowed(TextureSizeClass sizeClass);

// nullopt for an allowed size; otherwise a warning naming the texture, its size and the rule
// ("<name>: 128x128 exceeds the 64x64 maximum texture size ...").
std::optional<std::string> checkTextureSize(std::string_view name, uint32_t width, uint32_t height);

} // namespace zodar
