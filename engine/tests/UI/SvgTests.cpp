#include <doctest/doctest.h>

#include "../Support/UiTestDir.h"

#include "UI/Svg.h"

#include <filesystem>

using namespace zodar::ui;

namespace {
const std::filesystem::path kIcon =
    zodar::test::uiTestDir() / "Logo/LOGO_NoBackGround/White/SVG/512 X 512 LOGO_NoBackGround.svg";

bool hasPixel(const Image& img, uint8_t r, uint8_t g, uint8_t b, int tol) {
    for (size_t i = 0; i + 3 < img.pixels.size(); i += 4) {
        if (img.pixels[i + 3] == 255 && std::abs(img.pixels[i] - r) <= tol && std::abs(img.pixels[i + 1] - g) <= tol &&
            std::abs(img.pixels[i + 2] - b) <= tol) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("ui svg: original logo parses at its authored size") {
    const SvgDocument doc = SvgDocument::load(kIcon);
    CHECK(doc.size() == Size2{512, 512});
}

TEST_CASE("ui svg: rasterizes at any scale with brand colours") {
    const SvgDocument doc = SvgDocument::load(kIcon);
    for (float s : {0.125f, 0.5f, 1.33f}) {
        const Image img = doc.rasterize(s);
        CHECK(img.width == static_cast<uint32_t>(std::ceil(512 * s)));
        CHECK(img.pixels.size() == size_t{img.width} * img.height * 4);
    }
    const Image img = doc.rasterize(0.5f);
    CHECK(hasPixel(img, 255, 255, 255, 0));    // White head.
    CHECK(hasPixel(img, 0x18, 0xB7, 0x7A, 2)); // Green eye #18B77A.
}

TEST_CASE("ui svg: region crop") {
    const SvgDocument doc = SvgDocument::load(kIcon);
    const Image img = doc.rasterize(2.0f, Rect{50, 80, 100, 60});
    CHECK(img.width == 200);
    CHECK(img.height == 120);
}
