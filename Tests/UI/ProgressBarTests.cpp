#include <doctest/doctest.h>

#include "../Support/UiTestDir.h"

#include "UI/Widgets/ProgressBar.h"

using namespace zodar::ui;

TEST_CASE("ui progress: smoothing approaches but never exceeds the reported value") {
    float v = 0.0f;
    for (int i = 0; i < 10; ++i) {
        const float next = smoothProgress(v, 0.5f, 1.0f / 60.0f);
        CHECK(next >= v);
        CHECK(next <= 0.5f);
        v = next;
    }
    CHECK(v > 0.0f);
    for (int i = 0; i < 600; ++i) v = smoothProgress(v, 0.5f, 1.0f / 60.0f);
    CHECK(v == 0.5f);
    CHECK(smoothProgress(0.8f, 0.2f, 0.016f) == 0.2f); // Drop (retry): immediate.
    CHECK(smoothProgress(0.0f, 2.0f, 10.0f) == 1.0f);  // Clamped.
    CHECK(smoothProgress(0.3f, 0.6f, 0.0f) == 0.3f);   // No time, no movement.
}

TEST_CASE("ui progress: indeterminate segment sweeps across the track") {
    const Rect track{100, 10, 800, 2};
    const Rect start = indeterminateSegment(track, 0.0);
    const Rect mid = indeterminateSegment(track, kIndeterminatePeriod * 0.5);
    const Rect end = indeterminateSegment(track, kIndeterminatePeriod * 0.999);
    CHECK(start.width == doctest::Approx(800 * kIndeterminateWidth));
    CHECK(start.right() == doctest::Approx(100));      // Starts just left of the track.
    CHECK(end.x == doctest::Approx(900).epsilon(0.01)); // Ends just right of it.
    CHECK(mid.x > start.x);
    CHECK(mid.x < end.x);
    CHECK(indeterminateSegment(track, kIndeterminatePeriod).x == doctest::Approx(start.x)); // Loops.
}

TEST_CASE("ui progress: determinate and indeterminate drawing") {
    TextureStore textures;
    FontLibrary fonts(zodar::test::uiTestDir(), textures);
    Context ctx(fonts);
    InputState input;
    DrawList dl({0, 0, 1000, 100});
    ctx.beginFrame(dl, input, 1.0f);
    progressBar(ctx, {0, 0, 400, 2}, 0.25f, 0.0, color::kBrandAccent);
    REQUIRE(dl.vertices().size() == 8);                  // Track + fill.
    CHECK(dl.vertices()[5].x == doctest::Approx(100));   // Fill right edge = 25%.
    CHECK(unpackColor(dl.vertices()[4].color) == color::kBrandAccent);
    dl.reset({0, 0, 1000, 100});
    progressBar(ctx, {0, 0, 400, 2}, std::nullopt, 0.7, color::kBrandAccent);
    REQUIRE(dl.commands().size() == 2);                  // Track, then clipped segment.
    CHECK(dl.commands()[1].clip == Rect{0, 0, 400, 2});
    ctx.endFrame();
}
