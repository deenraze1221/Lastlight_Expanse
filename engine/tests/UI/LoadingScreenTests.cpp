#include <doctest/doctest.h>

#include "../Support/UiTestDir.h"

#include "UI/Screens/LoadingScreen.h"
#include "UI/Widgets/Button.h"

using namespace zodar::ui;

namespace {

constexpr Size2 kButton{100, 40};

struct Harness {
    TextureStore textures;
    FontLibrary fonts{zodar::test::uiTestDir(), textures};
    Context ctx{fonts};
    InputState input;
    DrawList list;
    LoadingScreen screen{zodar::test::uiTestDir()};
    Size2 window{960, 540};

    LoadingActions frame(const StartupSnapshot& s, double t = 0.0, float dt = 1.0f / 60.0f,
                         std::vector<InputEvent> events = {}) {
        input.beginFrame();
        for (const InputEvent& e : events) input.apply(e);
        list.reset({0, 0, window.width, window.height});
        ctx.beginFrame(list, input, LoadingScreen::scaleFor(window));
        const LoadingActions a = screen.draw(ctx, textures, s, nullptr, t, dt, engineVersionLabel());
        ctx.endFrame();
        return a;
    }
    // Press and release on the centre of a design-unit rectangle (two frames).
    LoadingActions click(const StartupSnapshot& s, Rect logical) {
        const float scale = LoadingScreen::scaleFor(window);
        const Vec2 p{(logical.x + logical.width * 0.5f) * scale, (logical.y + logical.height * 0.5f) * scale};
        InputEvent down{InputEvent::Type::PointerDown, p};
        InputEvent up{InputEvent::Type::PointerUp, p};
        frame(s, 0, 0.016f, {down});
        return frame(s, 0, 0.016f, {up});
    }
};

StartupSnapshot failedShaders() {
    StartupSnapshot s;
    s.stage = StartupStage::PreparingShaders;
    s.state = StartupState::Failed;
    s.error = "test failure";
    return s;
}

} // namespace

TEST_CASE("ui loading: layout of the running state") {
    StartupSnapshot s;
    const LoadingLayout l = computeLoadingLayout(s, false, kButton, kButton);
    CHECK_FALSE(l.showHint);
    CHECK_FALSE(l.showActions);
    CHECK_FALSE(l.showLog);
    CHECK(l.statusRow.bottom() == doctest::Approx(kLoadingStatusBottom));
    CHECK(l.track == Rect{64, 465, 832, 2});
    CHECK(l.version.y == doctest::Approx(kLoadingVersionTop));
    CHECK(l.track.x == doctest::Approx(64));
    CHECK(l.track.right() == doctest::Approx(960 - 64));
    CHECK(l.version.bottom() <= 540 - 18 + 0.5f);
}

TEST_CASE("ui loading: first-launch hint moves the status block up") {
    StartupSnapshot s;
    s.stage = StartupStage::PreparingShaders;
    s.stageElapsedSeconds = 31;
    const LoadingLayout l = computeLoadingLayout(s, false, kButton, kButton);
    CHECK(l.showHint);
    CHECK(l.hint.bottom() == doctest::Approx(kLoadingStatusBottom));
    CHECK(l.statusRow.bottom() <= l.hint.y);
    CHECK(l.track.bottom() <= l.statusRow.y);
}

TEST_CASE("ui loading: failure layout places Open log and Retry on the status row") {
    const LoadingLayout l = computeLoadingLayout(failedShaders(), true, {110, 40}, {90, 40});
    CHECK(l.showActions);
    CHECK(l.showLog);
    CHECK(l.retry.right() == doctest::Approx(960 - 64));
    CHECK(l.openLog.right() <= l.retry.x);
    CHECK(l.openLog.y >= l.statusRow.y);
    CHECK(l.retry.bottom() <= l.statusRow.bottom() + 0.01f);
    CHECK(l.logPanel.bottom() < l.track.y);
    CHECK(l.logPanel.y > 0);
    // Status block stays below the logo (logo bottom ~ y 408 in Loading.svg).
    CHECK(l.track.y > 408);
}

TEST_CASE("ui loading: approved window sizes map to proportional scales") {
    CHECK(LoadingScreen::scaleFor({960, 540}) == doctest::Approx(1.0));
    CHECK(LoadingScreen::scaleFor({1280, 720}) == doctest::Approx(1280.0 / 960.0));
    CHECK(LoadingScreen::scaleFor({1920, 1080}) == doctest::Approx(2.0));
}

TEST_CASE("ui loading: background keeps the original artwork except the C++ tag") {
    LoadingScreen screen(zodar::test::uiTestDir());
    size_t removed = 0;
    size_t kept = 0;
    for (const SvgShapeInfo& s : screen.artwork().shapes()) {
        if (LoadingScreen::keepBackgroundShape(s)) {
            ++kept;
        } else {
            ++removed;
            CHECK(s.fill.a < 64); // The tag is the faint (10% white) "C++" glyphs.
        }
    }
    CHECK(removed == 3);
    CHECK(kept > 10);
}

TEST_CASE("ui loading: version label comes from the build") {
    CHECK(engineVersionLabel() == "ZODAR Engine v0.1");
}

TEST_CASE("ui loading: progress is displayed only as reported") {
    Harness h;
    StartupSnapshot s; // No overall progress reported.
    h.frame(s, 0, 1.0f);
    CHECK(h.screen.displayedProgress() == 0.0f);
    s.overall = 0.5f;
    for (int i = 0; i < 120; ++i) h.frame(s, i / 60.0, 1.0f / 60.0f);
    CHECK(h.screen.displayedProgress() == 0.5f);
    s.overall = 0.2f; // Source went back (e.g. after retry): no stale value shown.
    h.frame(s);
    CHECK(h.screen.displayedProgress() == 0.2f);
    s.overall.reset();
    s.state = StartupState::Completed;
    h.frame(s);
    CHECK(h.screen.displayedProgress() == 1.0f);
}

TEST_CASE("ui loading: Retry and Open log are clickable only on failure") {
    Harness h;
    const StartupSnapshot f = failedShaders();
    h.frame(f);
    const LoadingLayout l = computeLoadingLayout(f, false, measureButton(h.ctx, "Open log"), measureButton(h.ctx, "Retry"));
    REQUIRE(l.showActions);

    LoadingActions a = h.click(f, l.openLog);
    CHECK(a.toggledLog);
    CHECK(h.screen.logOpen());
    const LoadingLayout open = computeLoadingLayout(f, true, measureButton(h.ctx, "Hide log"), measureButton(h.ctx, "Retry"));
    a = h.click(f, open.openLog);
    CHECK(a.toggledLog);
    CHECK_FALSE(h.screen.logOpen());

    a = h.click(f, l.retry);
    CHECK(a.retry);

    // Running state: the same spot does nothing.
    StartupSnapshot running;
    a = h.click(running, l.retry);
    CHECK_FALSE(a.retry);
    CHECK_FALSE(a.toggledLog);
}

TEST_CASE("ui loading: works at every approved window size and odd sizes") {
    Harness h;
    StartupSnapshot s;
    s.overall = 0.3f;
    for (Size2 w : {Size2{960, 540}, Size2{1280, 720}, Size2{1920, 1080}, Size2{1100, 619}}) {
        h.window = w;
        h.frame(s);
        CHECK(!h.list.commands().empty());
        // Nothing drawn outside the window except the oversized background fill.
        for (const DrawVertex& v : h.list.vertices()) {
            if (v.x > 1.0e5f || v.y > 1.0e5f) continue; // Full-window background fill.
            CHECK(v.x <= w.width + 1.0f);
            CHECK(v.y <= w.height + 1.0f);
        }
    }
}
