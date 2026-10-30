#include <doctest/doctest.h>

#include "../Support/UiTestDir.h"

#include "UI/Svg.h"

using namespace zodar::ui;

namespace {
const std::filesystem::path kLoading = zodar::test::uiTestDir() / "Logo/Loading/Loading.svg";

size_t count(const Image& img, Color c, int tol) {
    size_t n = 0;
    for (size_t i = 0; i + 3 < img.pixels.size(); i += 4) {
        if (img.pixels[i + 3] > 200 && std::abs(img.pixels[i] - c.r) <= tol && std::abs(img.pixels[i + 1] - c.g) <= tol &&
            std::abs(img.pixels[i + 2] - c.b) <= tol) {
            ++n;
        }
    }
    return n;
}
} // namespace

TEST_CASE("ui svg: mask definitions are removed in memory only") {
    std::string text = "<svg><mask id='a'><rect fill='white'/></mask><mask id='b'/><maskfoo/><path mask='url(#a)'/></svg>";
    CHECK(stripMaskDefinitions(text) == 2);
    CHECK(text == "<svg><maskfoo/><path mask='url(#a)'/></svg>");
    std::string none = "<svg><rect/></svg>";
    CHECK(stripMaskDefinitions(none) == 0);
}

TEST_CASE("ui svg: original Loading.svg parses without drawing mask contents") {
    const SvgDocument doc = SvgDocument::load(kLoading);
    CHECK(doc.size() == Size2{960, 540});
    CHECK(doc.strippedMasks() == 4);
    // Without stripping, nanosvg would paint the 4 mask rectangles (962x130, white) as shapes.
    for (const SvgShapeInfo& s : doc.shapes()) {
        CHECK_FALSE((s.hasFill && s.fill == Color::hex(0xFFFFFF) && s.bounds.width > 900.0f));
    }
    const Image img = doc.rasterize(0.5f);
    CHECK(count(img, Color::hex(0xFFFFFF), 0) < img.pixels.size() / 4 / 20); // White = logo only (< 5% of pixels).
}

TEST_CASE("ui svg: shape list and filtered rasterization") {
    const SvgDocument doc = SvgDocument::load(kLoading);
    const auto shapes = doc.shapes();
    size_t strokes = 0;
    for (const SvgShapeInfo& s : shapes) {
        if (s.hasStroke && s.stroke == Color::hex(0x151515)) ++strokes;
    }
    CHECK(strokes == 4); // The four "Z O D A R" outline rows.

    // Keep everything except green: the eye disappears, the rest stays.
    const Image all = doc.rasterize(1.0f);
    const Image noGreen = doc.rasterize(1.0f, std::nullopt, [](const SvgShapeInfo& s) {
        return !(s.hasFill && s.fill.g > 150 && s.fill.r < 60);
    });
    CHECK(count(all, Color::hex(0x18B77A), 3) > 50);
    CHECK(count(noGreen, Color::hex(0x18B77A), 3) == 0);
    CHECK(count(noGreen, Color::hex(0xFFFFFF), 0) >= count(all, Color::hex(0xFFFFFF), 0));
    // The document itself is unchanged by filtering.
    CHECK(doc.rasterize(1.0f).pixels == all.pixels);
}
