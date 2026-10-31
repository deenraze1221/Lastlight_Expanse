#include <doctest/doctest.h>

#include "UI/Scale.h"

using namespace zodar::ui;

TEST_CASE("ui scale: density follows display scale continuously") {
    for (float d : {1.0f, 1.25f, 1.33f, 1.5f, 1.75f, 2.0f, 2.5f}) {
        CHECK(computeScale(ScalePolicy::Density, {{1920, 1080}, d, 1.0f}) == doctest::Approx(d));
    }
    CHECK(computeScale(ScalePolicy::Density, {{1920, 1080}, 1.5f, 1.2f}) == doctest::Approx(1.8f));
}

TEST_CASE("ui scale: loading window profiles fit a 960x540 design") {
    const Size2 design{960, 540};
    CHECK(computeScale(ScalePolicy::FitDesign, {{960, 540}, 1, 1, design}) == doctest::Approx(1.0f));
    CHECK(computeScale(ScalePolicy::FitDesign, {{1280, 720}, 1, 1, design}) == doctest::Approx(1.3333f).epsilon(0.001));
    CHECK(computeScale(ScalePolicy::FitDesign, {{1920, 1080}, 1, 1, design}) == doctest::Approx(2.0f));
    // Non-16:9 windows fit the limiting axis.
    CHECK(computeScale(ScalePolicy::FitDesign, {{1920, 600}, 1, 1, design}) == doctest::Approx(600.0f / 540.0f));
}

TEST_CASE("ui scale: clamped, sanitized and quantized") {
    CHECK(computeScale(ScalePolicy::Density, {{100, 100}, 10.0f, 1.0f}) == doctest::Approx(kMaxScale));
    CHECK(computeScale(ScalePolicy::Density, {{100, 100}, 0.1f, 1.0f}) == doctest::Approx(kMinScale));
    CHECK(computeScale(ScalePolicy::Density, {{100, 100}, 0.0f, 0.0f}) == doctest::Approx(1.0f));
    CHECK(quantizeScale(1.33f) == doctest::Approx(1.35f));
    CHECK(quantizeScale(1.26f) == doctest::Approx(1.25f));
    const Size2 l = logicalSize({2560, 1600}, 2.0f);
    CHECK(l.width == doctest::Approx(1280));
    CHECK(l.height == doctest::Approx(800));
}
