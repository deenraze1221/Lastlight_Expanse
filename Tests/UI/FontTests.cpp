#include <doctest/doctest.h>

#include "../Support/UiTestDir.h"

#include "UI/Font.h"

using namespace zodar::ui;

TEST_CASE("ui font: UTF-8 decoding") {
    const auto cps = decodeUtf8("A\xC2\xB7\xE2\x80\x94\xF0\x9F\x99\x82");
    REQUIRE(cps.size() == 4);
    CHECK(cps[0] == 'A');
    CHECK(cps[1] == 0x00B7);
    CHECK(cps[2] == 0x2014);
    CHECK(cps[3] == 0x1F642);
}

TEST_CASE("ui font: malformed UTF-8 becomes U+FFFD without swallowing ASCII") {
    // Truncated 2-byte lead, overlong '/', encoded surrogate, stray continuation byte.
    const auto cps = decodeUtf8("\xC2" "A" "\xC0\xAF" "\xED\xA0\x80" "\x80" "B");
    REQUIRE(cps.size() == 9);
    CHECK(cps[0] == 0xFFFD);
    CHECK(cps[1] == 'A');
    CHECK(cps[2] == 0xFFFD);
    CHECK(cps[3] == 0xFFFD);
    CHECK(cps[4] == 0xFFFD);
    CHECK(cps[5] == 0xFFFD);
    CHECK(cps[6] == 0xFFFD);
    CHECK(cps[7] == 0xFFFD);
    CHECK(cps[8] == 'B');
}

TEST_CASE("ui font: measuring scales with UI scale and caches atlases per size") {
    TextureStore textures;
    FontLibrary fonts(zodar::test::uiTestDir(), textures);
    const Size2 a = fonts.measure(text::kBody, 1.0f, "Recent Projects");
    const Size2 b = fonts.measure(text::kBody, 2.0f, "Recent Projects");
    CHECK(a.width > 50.0f);
    CHECK(b.width == doctest::Approx(a.width * 2.0f).epsilon(0.06));
    CHECK(a.height == doctest::Approx(std::round(16.0f * 1.3f)));
    CHECK(fonts.atlasCount() == 2);
    fonts.measure(text::kBody, 1.0f, "again");
    CHECK(fonts.atlasCount() == 2);
    // Any scale works, not only the test profiles.
    const Size2 odd = fonts.measure(text::kBody, 1.37f, "Recent Projects");
    CHECK(odd.width > a.width);
    CHECK(odd.width < b.width);
}

TEST_CASE("ui font: letter spacing, uppercase and faces") {
    TextureStore textures;
    FontLibrary fonts(zodar::test::uiTestDir(), textures);
    TextStyle tight = text::kButton;
    tight.letterSpacing = 0.0f;
    CHECK(fonts.measure(text::kButton, 1, "OPEN").width > fonts.measure(tight, 1, "OPEN").width);
    CHECK(fonts.measure(text::kButton, 1, "open").width == fonts.measure(text::kButton, 1, "OPEN").width);
    TextStyle light = text::kBody;
    light.face = FontFace::RajdhaniLight;
    TextStyle bold = text::kBody;
    bold.face = FontFace::RajdhaniBold;
    CHECK(fonts.measure(bold, 1, "Settings").width != fonts.measure(light, 1, "Settings").width);
    CHECK(fonts.ascent(text::kDisplay, 1.0f) > 0.0f);
}

TEST_CASE("ui font: drawing emits textured quads on whole pixels") {
    TextureStore textures;
    FontLibrary fonts(zodar::test::uiTestDir(), textures);
    DrawList dl;
    const float w = fonts.draw(dl, {10.3f, 20.6f}, text::kTitle, 1.33f, color::kBrandPrimary, "ZODAR · v0.1");
    CHECK(w > 0.0f);
    REQUIRE(!dl.commands().empty());
    CHECK(dl.commands()[0].texture != kNoTexture);
    for (const DrawVertex& v : dl.vertices()) {
        CHECK(v.x == std::round(v.x));
        CHECK(v.y == std::round(v.y));
    }
}
