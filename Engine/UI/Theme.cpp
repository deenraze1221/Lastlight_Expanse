#include "UI/Theme.h"

namespace zodar::ui {

const std::array<ColorToken, 10>& colorTokens() {
    static const std::array<ColorToken, 10> tokens{{
        {"brand/background", color::kBrandBackground, true},
        {"brand/surface", color::kBrandSurface, true},
        {"brand/primary", color::kBrandPrimary, true},
        {"brand/accent", color::kBrandAccent, true},
        {"brand/error", color::kBrandError, true},
        {"ui/surface-raised", color::kSurfaceRaised, false},
        {"ui/border", color::kBorder, false},
        {"ui/text-secondary", color::kTextSecondary, false},
        {"ui/text-muted", color::kTextMuted, false},
        {"ui/accent-hover", color::kAccentHover, false},
    }};
    return tokens;
}

std::string_view fontFile(FontFace face) {
    switch (face) {
    case FontFace::MichromaRegular: return "Fonts/Michroma/Michroma-Regular.ttf";
    case FontFace::RajdhaniLight: return "Fonts/Rajdhani/Rajdhani-Light.ttf";
    case FontFace::RajdhaniRegular: return "Fonts/Rajdhani/Rajdhani-Regular.ttf";
    case FontFace::RajdhaniBold: return "Fonts/Rajdhani/Rajdhani-Bold.ttf";
    case FontFace::Count: break;
    }
    return {};
}

std::string_view fontFaceName(FontFace face) {
    switch (face) {
    case FontFace::MichromaRegular: return "Michroma Regular 400";
    case FontFace::RajdhaniLight: return "Rajdhani Light 300";
    case FontFace::RajdhaniRegular: return "Rajdhani Regular 400";
    case FontFace::RajdhaniBold: return "Rajdhani Bold 700";
    case FontFace::Count: break;
    }
    return {};
}

const std::array<TextStyle, 9>& textStyles() {
    static const std::array<TextStyle, 9> styles{text::kDisplay, text::kHeading, text::kLabel,
                                                 text::kTitle,   text::kBody,    text::kCaption,
                                                 text::kOverline, text::kButton, text::kPath};
    return styles;
}

} // namespace zodar::ui
