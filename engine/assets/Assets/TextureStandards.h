#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Texture size standards (docs/ART_DIRECTION.md §2). Pixel-art textures are square powers of
// two: 32×32 is the default for every asset, 64×64 the absolute maximum (characters, weapons
// and unique assets included). Smaller power-of-two squares (8, 16) are allowed for tiny
// details. Anything else is a violation: it still loads (textures are never resized), but
// the asset manager, asset checks and tools report a warning (256 and above: an error in the
// asset checks, Phase V2).
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

// --- Configurable standards (Phase V2) -----------------------------------------------------
//
// A texture standard names texture classes and the square power-of-two sizes each class may
// use, the class of textures that declare none, a prohibited size and the texel density
// target. Registries declare one in assets.json ("textureStandards"); registries without one
// use engineDefault(), which reproduces the rule above (and its messages). Textures of the
// prohibited size and above (either side; 256 by the GDD) are an error under every standard.

inline constexpr uint32_t kTextureSizeProhibited = 256;

struct TextureClassRule {
    std::string name;
    std::vector<uint32_t> sizes; // allowed square sizes (powers of two, ascending, unique)
    bool operator==(const TextureClassRule&) const = default;
};

struct TextureStandard {
    std::vector<TextureClassRule> classes; // declaration order
    std::string defaultClass;              // class of textures without "textureClass"
    uint32_t prohibitedSize = kTextureSizeProhibited; // >= this on either side: error (<= 256)
    double densityTarget = 32.0;           // texels per metre (environment models, blocks)
    double densityTolerance = 2.0;         // warn outside [target / tol, target * tol]; <= 0 disables
    bool engineRule = false;               // engineDefault(): keep the legacy 32/64 messages

    // One class "default" = {8, 16, 32, 64}, density 32 (x2), prohibited 256.
    static TextureStandard engineDefault();

    const TextureClassRule* find(std::string_view name) const;
    // Throws zodar::Error naming the problem: no classes, empty or duplicate class name, sizes
    // not powers of two, not ascending/unique, at or above prohibitedSize, prohibitedSize above
    // 256 or not above every allowed size, unknown default class, non-positive density target.
    void validate() const;
    bool operator==(const TextureStandard&) const = default;
};

enum class TextureSizeVerdict : uint8_t {
    Allowed,
    Violation,  // warning (fails --strict): wrong size for the class, non-square, not a power of two
    Prohibited, // error: at or above the prohibited size
};

struct TextureSizeIssue {
    TextureSizeVerdict verdict = TextureSizeVerdict::Violation;
    std::string message; // "<name>: <w>x<h> ..."
};

// Checks one texture of class `textureClass` (empty: the standard's default class). An unknown
// class is a Violation naming it (registries reject unknown classes when they load).
std::optional<TextureSizeIssue> checkTextureSize(const TextureStandard& standard, std::string_view name,
                                                 uint32_t width, uint32_t height, std::string_view textureClass = {});

// Runtime form (no asset class known, e.g. AssetManager loads by path): allowed when any class
// of the standard allows the size; the prohibited size is still an error verdict. The engine
// default gives the legacy messages.
std::optional<TextureSizeIssue> checkTextureSizeAnyClass(const TextureStandard& standard, std::string_view name,
                                                         uint32_t width, uint32_t height);

// Engine default rule: nullopt for an allowed size; otherwise the message naming the texture,
// its size and the rule ("<name>: 128x128 exceeds the 64x64 maximum texture size ...";
// 256 and above: "... is prohibited ...").
std::optional<std::string> checkTextureSize(std::string_view name, uint32_t width, uint32_t height);

} // namespace zodar
