#include <doctest/doctest.h>

#include "../Support/UiTestDir.h"

#include "UI/Theme.h"

#include <filesystem>
#include <set>

using namespace zodar::ui;

TEST_CASE("ui: colour tokens match the approved palette") {
    CHECK(color::kBrandBackground.rgb() == 0x111111);
    CHECK(color::kBrandSurface.rgb() == 0x181818);
    CHECK(color::kBrandPrimary.rgb() == 0xFFFFFF);
    CHECK(color::kBrandAccent.rgb() == 0x18B77A);
    CHECK(color::kBrandError.rgb() == 0xB7181B);
    CHECK(color::kSurfaceRaised.rgb() == 0x1F1F1F);
    CHECK(color::kBorder.rgb() == 0x2A2A2A);
    CHECK(color::kTextSecondary.rgb() == 0x9A9F9C);
    CHECK(color::kTextMuted.rgb() == 0x5C615E);
    CHECK(color::kAccentHover.rgb() == 0x1FCB88);

    int official = 0;
    std::set<std::string_view> names;
    for (const ColorToken& t : colorTokens()) {
        official += t.official ? 1 : 0;
        names.insert(t.name);
        CHECK(t.value.a == 255);
    }
    CHECK(official == 5);
    CHECK(names.size() == 10);
}

TEST_CASE("ui: text styles use only the four approved faces") {
    for (const TextStyle& s : textStyles()) {
        CHECK(static_cast<size_t>(s.face) < kFontFaceCount);
        CHECK(s.size > 0.0f);
        CHECK(s.lineHeight >= 1.0f);
    }
    CHECK(kFontFaceCount == 4);
    CHECK(fontFaceName(FontFace::MichromaRegular) == "Michroma Regular 400");
    CHECK(fontFaceName(FontFace::RajdhaniLight) == "Rajdhani Light 300");
    CHECK(fontFaceName(FontFace::RajdhaniRegular) == "Rajdhani Regular 400");
    CHECK(fontFaceName(FontFace::RajdhaniBold) == "Rajdhani Bold 700");
}

TEST_CASE("ui: every font file and its licence exist") {
    const std::filesystem::path dir = zodar::test::uiTestDir();
    for (size_t i = 0; i < kFontFaceCount; ++i) {
        CHECK(std::filesystem::exists(dir / std::string(fontFile(static_cast<FontFace>(i)))));
    }
    CHECK(std::filesystem::exists(dir / "Fonts/Michroma/OFL.txt"));
    CHECK(std::filesystem::exists(dir / "Fonts/Rajdhani/OFL.txt"));
}
