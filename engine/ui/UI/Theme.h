#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace zodar::ui {

// sRGB colour with straight (non-premultiplied) alpha, exactly as authored in the design
// tools. Backends that render into linear or sRGB-encoding targets must convert.
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    static constexpr Color hex(uint32_t rgb, uint8_t alpha = 255) {
        return {static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                static_cast<uint8_t>(rgb & 0xFF), alpha};
    }
    constexpr Color withAlpha(float alpha) const {
        const float clamped = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
        return {r, g, b, static_cast<uint8_t>(static_cast<float>(a) * clamped + 0.5f)};
    }
    constexpr uint32_t rgb() const { return (uint32_t{r} << 16) | (uint32_t{g} << 8) | b; }
    friend constexpr bool operator==(const Color&, const Color&) = default;
};

// Design tokens. Names match the Figma variables ("brand/accent", "ui/border", ...).
namespace color {
// Official brand palette (unchanged).
inline constexpr Color kBrandBackground = Color::hex(0x111111);
inline constexpr Color kBrandSurface = Color::hex(0x181818);
inline constexpr Color kBrandPrimary = Color::hex(0xFFFFFF);
inline constexpr Color kBrandAccent = Color::hex(0x18B77A);
inline constexpr Color kBrandError = Color::hex(0xB7181B);
// Approved UI additions (slightly green-tinted greys; hover green for hover states only).
inline constexpr Color kSurfaceRaised = Color::hex(0x1F1F1F);
inline constexpr Color kBorder = Color::hex(0x2A2A2A);
inline constexpr Color kTextSecondary = Color::hex(0x9A9F9C);
inline constexpr Color kTextMuted = Color::hex(0x5C615E);
inline constexpr Color kAccentHover = Color::hex(0x1FCB88);
} // namespace color

struct ColorToken {
    std::string_view name;
    Color value;
    bool official; // Brand palette (true) or approved UI addition (false).
};
const std::array<ColorToken, 10>& colorTokens();

// Approved typography: exactly these four faces.
enum class FontFace : uint8_t { MichromaRegular, RajdhaniLight, RajdhaniRegular, RajdhaniBold, Count };
inline constexpr size_t kFontFaceCount = static_cast<size_t>(FontFace::Count);

// Path of the face's file relative to the UI resource directory.
std::string_view fontFile(FontFace face);
std::string_view fontFaceName(FontFace face);

// Text style in design units (1 unit = 1 px at UI scale 1.0).
struct TextStyle {
    std::string_view name;
    FontFace face = FontFace::RajdhaniRegular;
    float size = 16.0f;
    float lineHeight = 1.3f;    // Multiple of size.
    float letterSpacing = 0.0f; // Fraction of size (Figma "percent" / 100).
    bool uppercase = false;
};

namespace text {
inline constexpr TextStyle kDisplay{"Brand/Display", FontFace::MichromaRegular, 28.0f, 1.2f, 0.08f, false};
inline constexpr TextStyle kHeading{"Brand/Heading", FontFace::MichromaRegular, 18.0f, 1.3f, 0.06f, false};
inline constexpr TextStyle kLabel{"Brand/Label", FontFace::MichromaRegular, 11.0f, 1.3f, 0.24f, false};
inline constexpr TextStyle kTitle{"UI/Title", FontFace::RajdhaniBold, 20.0f, 1.2f, 0.02f, false};
inline constexpr TextStyle kBody{"UI/Body", FontFace::RajdhaniRegular, 16.0f, 1.3f, 0.01f, false};
inline constexpr TextStyle kCaption{"UI/Caption", FontFace::RajdhaniRegular, 13.0f, 1.3f, 0.04f, false};
inline constexpr TextStyle kOverline{"UI/Overline", FontFace::RajdhaniBold, 12.0f, 1.3f, 0.18f, true};
inline constexpr TextStyle kButton{"UI/Button", FontFace::RajdhaniBold, 14.0f, 1.0f, 0.12f, true};
inline constexpr TextStyle kPath{"UI/Path", FontFace::RajdhaniRegular, 13.0f, 1.4f, 0.0f, false};
} // namespace text
const std::array<TextStyle, 9>& textStyles();

// Shared metrics (design units).
namespace metric {
inline constexpr float kRadius = 2.0f;
inline constexpr float kBorderWidth = 1.0f;
inline constexpr float kControlHeight = 40.0f;
} // namespace metric

} // namespace zodar::ui
