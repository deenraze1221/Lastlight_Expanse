#include <doctest/doctest.h>

#include "Renderer/Camera.h"
#include "Renderer/Frustum.h"

using namespace zodar;
using namespace zodar::gfx;

namespace {

Bounds box(glm::vec3 c, float h) {
    Bounds b;
    b.expand(c - glm::vec3(h));
    b.expand(c + glm::vec3(h));
    return b;
}

} // namespace

TEST_CASE("frustum: infinite reverse-Z perspective culling") {
    Camera camera;
    camera.position = {0, 0, 0};
    const Frustum f(camera.projectionMatrix(1.0f) * camera.viewMatrix());
    CHECK(f.intersects(box({0, 0, -10}, 1)));
    CHECK(f.intersects(box({0, 0, -100000}, 1)));   // No far plane.
    CHECK_FALSE(f.intersects(box({0, 0, 10}, 1)));  // Behind the camera.
    CHECK_FALSE(f.intersects(box({50, 0, -10}, 1))); // Far to the right.
    CHECK(f.intersects(box({6, 0, -10}, 1)));        // Straddles the right plane (60 deg FOV).
    CHECK_FALSE(f.intersects(Bounds{}));
}

TEST_CASE("frustum: finite far plane") {
    Camera camera;
    camera.projection.farPlane = 50.0f;
    const Frustum f(camera.projectionMatrix(1.0f) * camera.viewMatrix());
    CHECK(f.intersects(box({0, 0, -49}, 0.5f)));
    CHECK_FALSE(f.intersects(box({0, 0, -60}, 1)));
}
