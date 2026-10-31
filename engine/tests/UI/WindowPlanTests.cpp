#include <doctest/doctest.h>

#include "UI/WindowPlan.h"

using namespace zodar::ui;

namespace {
DisplayArea monitor(float w, float h, float taskbar = 48.0f) {
    return {{0, 0, w, h}, {0, 0, w, h - taskbar}};
}
} // namespace

TEST_CASE("ui window plan: approved loading sizes per monitor") {
    CHECK(loadingWindowRect(monitor(1920, 1080)).width == 960);
    CHECK(loadingWindowRect(monitor(1920, 1080)).height == 540);
    CHECK(loadingWindowRect(monitor(2560, 1440)).width == 1280);
    CHECK(loadingWindowRect(monitor(2560, 1440)).height == 720);
    CHECK(loadingWindowRect(monitor(3840, 2160)).width == 1920);
    CHECK(loadingWindowRect(monitor(3840, 2160)).height == 1080);
}

TEST_CASE("ui window plan: loading window is centred, 16:9 and never fullscreen") {
    const DisplayArea d = monitor(2560, 1440);
    const Rect r = loadingWindowRect(d);
    CHECK(r.x == 640);
    CHECK(r.y == std::round((1392 - 720) * 0.5f));
    for (const DisplayArea& m : {monitor(1366, 768), monitor(1280, 720), monitor(1680, 1050), monitor(3440, 1440),
                                 monitor(5120, 2880), monitor(1024, 600, 0)}) {
        const Rect w = loadingWindowRect(m);
        CHECK(w.width * 9.0f == doctest::Approx(w.height * 16.0f));
        CHECK(w.width <= m.usable.width * 0.9f + 0.5f);
        CHECK(w.height <= m.usable.height * 0.9f + 0.5f);
        CHECK(w.x >= m.usable.x);
        CHECK(w.y >= m.usable.y);
    }
    // Between profiles: the largest that is at most half the monitor.
    CHECK(loadingWindowRect(monitor(3440, 1440)).width == 1280); // Ultrawide 1440p.
    CHECK(loadingWindowRect(monitor(5120, 2880)).width == 1920); // 5K: no larger profile exists.
    CHECK(loadingWindowRect(monitor(1680, 1050)).width == 960);  // Smaller than 1080p.
    CHECK(loadingWindowRect(monitor(1024, 600, 0)).width < 960); // Shrunk to fit 90% of usable area.
}

TEST_CASE("ui window plan: offset monitors (second display)") {
    const DisplayArea d{{1920, 0, 2560, 1440}, {1920, 0, 2560, 1400}};
    const Rect r = loadingWindowRect(d);
    CHECK(r.x == 1920 + 640);
    CHECK(r.y == 340);
}

TEST_CASE("ui window plan: launcher after loading is larger, centred, capped") {
    const DisplayArea d = monitor(1920, 1080);
    const Rect r = launcherWindowRect(d, 1.0f);
    CHECK(r.width > loadingWindowRect(d).width);
    CHECK(r.width <= 1920 * 0.85f);
    CHECK(r.height <= (1080 - 48) * 0.85f + 0.5f);
    CHECK(r.width * 9.0f == doctest::Approx(r.height * 16.0f));
    CHECK(r.x + r.width * 0.5f == doctest::Approx(960).epsilon(0.001));
    // Large monitor at 100%: capped at 1920x1080 design units.
    CHECK(launcherWindowRect(monitor(5120, 2880), 1.0f).width == 1920);
    // Same monitor at 200%: cap scales with display density.
    CHECK(launcherWindowRect(monitor(5120, 2880), 2.0f).width > 1920);
}
