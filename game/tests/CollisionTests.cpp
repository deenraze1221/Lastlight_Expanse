// CollisionWorld (temporary AABB collision, Phase D decision 2) and the temporary exploration
// walker: blocking, sliding, raycasts, gravity, jumping, stair step-up.
#include "Physics/CollisionWorld.h"
#include "Player/ExplorationWalker.h"

#include <doctest/doctest.h>

using namespace lastlight;

namespace {

CollisionWorld room() {
    CollisionWorld w;
    w.addStatic({{-10, -0.1f, -10}, {10, 0, 10}}); // floor
    w.addStatic({{2, 0, -10}, {2.3f, 3, 10}});     // wall at x = 2
    w.addStatic({{-10, 3.6f, -10}, {10, 3.7f, 10}}); // ceiling
    return w;
}

void run(ExplorationWalker& walker, const CollisionWorld& w, WalkInput input, float seconds) {
    for (int i = 0; i < int(seconds * 60.0f); ++i) {
        walker.step(w, input, 1.0f / 60.0f);
    }
}

} // namespace

TEST_CASE("collision: sweep stops at boxes per axis and slides along walls") {
    const CollisionWorld w = room();
    const Aabb player{{0.0f, 0.001f, 0.0f}, {0.6f, 1.8f, 0.6f}};
    const auto r = w.sweep(player, {5.0f, 0.0f, 1.0f});
    CHECK(r.hitX);
    CHECK_FALSE(r.hitZ);
    CHECK(r.moved.x == doctest::Approx(1.4f - 0.001f));
    CHECK(r.moved.z == doctest::Approx(1.0f)); // slides along the wall
    const auto down = w.sweep(player, {0.0f, -1.0f, 0.0f});
    CHECK(down.grounded());
    CHECK(down.moved.y == doctest::Approx(0.0f).epsilon(0.01));
    // Overlapping boxes never trap the mover.
    const Aabb inside{{2.0f, 0.5f, 0.0f}, {2.2f, 1.0f, 0.2f}};
    CHECK(w.sweep(inside, {-1.0f, 0.0f, 0.0f}).moved.x == doctest::Approx(-1.0f));
}

TEST_CASE("collision: raycast returns the nearest hit with its normal") {
    const CollisionWorld w = room();
    const auto hit = w.raycast({0, 1, 0}, {1, 0, 0}, 10.0f);
    REQUIRE(hit);
    CHECK(hit->distance == doctest::Approx(2.0f));
    CHECK(hit->normal == glm::vec3(-1, 0, 0));
    CHECK_FALSE(w.raycast({0, 1, 0}, {1, 0, 0}, 1.5f));
    const auto floor = w.raycast({0, 1, 0}, {0, -1, 0}, 10.0f);
    REQUIRE(floor);
    CHECK(floor->normal == glm::vec3(0, 1, 0));
}

TEST_CASE("walker: lands, walks, sprints, is stopped by walls and jumps") {
    const CollisionWorld w = room();
    ExplorationWalker walker;
    walker.spawn({0, 1.0f, 0});
    run(walker, w, {}, 1.0f);
    CHECK(walker.grounded());
    CHECK(walker.feet().y == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(walker.eye().y == doctest::Approx(1.65f).epsilon(0.01));

    WalkInput forward;
    forward.forward = 1.0f;
    forward.yaw = 0.0f; // -z
    run(walker, w, forward, 1.0f);
    CHECK(walker.feet().z < -3.0f);
    CHECK(walker.feet().z > -4.1f);
    forward.sprint = true;
    const float z0 = walker.feet().z;
    run(walker, w, forward, 0.5f);
    CHECK(z0 - walker.feet().z > 3.0f); // sprint ~7 m/s

    WalkInput east;
    east.forward = 1.0f;
    east.yaw = -1.5707963f; // +x
    run(walker, w, east, 2.0f);
    CHECK(walker.feet().x == doctest::Approx(2.0f - 0.3f).epsilon(0.01)); // stopped by the wall

    WalkInput jump;
    jump.jump = true;
    float peak = 0.0f;
    for (int i = 0; i < 30; ++i) {
        walker.step(w, jump, 1.0f / 60.0f);
        peak = std::max(peak, walker.feet().y);
    }
    CHECK(peak > 0.6f);
    run(walker, w, {}, 1.0f);
    CHECK(walker.grounded());
}

TEST_CASE("walker: climbs stair steps but not a table-height ledge") {
    CollisionWorld w = room();
    for (int k = 0; k < 6; ++k) { // 0.2 m steps along -z
        w.addStatic({{-1, 0, -2.0f - 0.67f * (k + 1)}, {1, 0.2f * (k + 1), -2.0f - 0.67f * k}});
    }
    w.addStatic({{-1, 0, -10}, {1, 1.2f, -6.0f}}); // landing
    w.addStatic({{-10, 0, -10.3f}, {10, 3, -10}}); // end wall
    ExplorationWalker walker;
    walker.spawn({0, 0.01f, 0});
    WalkInput forward;
    forward.forward = 1.0f;
    run(walker, w, forward, 3.0f);
    CHECK(walker.feet().y == doctest::Approx(1.2f).epsilon(0.02));
    CHECK(walker.feet().z < -6.0f);

    CollisionWorld table = room();
    table.addStatic({{-1, 0, -2}, {1, 0.76f, -1}});
    walker.spawn({0, 0.01f, 0});
    run(walker, table, forward, 2.0f);
    CHECK(walker.feet().y == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(walker.feet().z > -0.75f);
}
