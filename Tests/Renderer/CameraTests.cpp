#include <doctest/doctest.h>

#include "Renderer/Camera.h"

#include <glm/gtc/constants.hpp>

using namespace raven::gfx;

namespace {

glm::vec3 project(const glm::mat4& m, glm::vec3 p) {
    const glm::vec4 clip = m * glm::vec4(p, 1.0f);
    return glm::vec3(clip) / clip.w;
}

} // namespace

TEST_CASE("camera: infinite reverse-Z perspective depth range") {
    const glm::mat4 p = perspectiveReverseZ(glm::radians(90.0f), 1.0f, 0.1f, 0.0f);
    CHECK(project(p, {0, 0, -0.1f}).z == doctest::Approx(1.0f));
    CHECK(project(p, {0, 0, -1000.0f}).z == doctest::Approx(0.0001f).epsilon(0.01));
    CHECK(project(p, {0, 0, -1.0f}).z > project(p, {0, 0, -2.0f}).z); // Closer = larger depth.
}

TEST_CASE("camera: finite reverse-Z perspective maps near->1, far->0") {
    const glm::mat4 p = perspectiveReverseZ(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 200.0f);
    CHECK(project(p, {0, 0, -0.5f}).z == doctest::Approx(1.0f));
    CHECK(project(p, {0, 0, -200.0f}).z == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("camera: Vulkan Y-down clip space and aspect") {
    const glm::mat4 p = perspectiveReverseZ(glm::radians(90.0f), 2.0f, 0.1f, 0.0f);
    // A point above the view axis lands in the upper half: negative NDC y.
    CHECK(project(p, {0, 1, -1}).y == doctest::Approx(-1.0f));
    // With aspect 2 the horizontal edge is at x = 2 at distance 1.
    CHECK(project(p, {2, 0, -1}).x == doctest::Approx(1.0f));
}

TEST_CASE("camera: orthographic reverse-Z") {
    const glm::mat4 o = orthographicReverseZ(10.0f, 1.0f, 1.0f, 101.0f);
    CHECK(project(o, {0, 0, -1}).z == doctest::Approx(1.0f));
    CHECK(project(o, {0, 0, -101}).z == doctest::Approx(0.0f));
    CHECK(project(o, {5, 5, -50}).x == doctest::Approx(1.0f));
    CHECK(project(o, {5, 5, -50}).y == doctest::Approx(-1.0f));
}

TEST_CASE("camera: view matrix, lookAt and yaw/pitch") {
    Camera camera;
    camera.position = {0, 2, 5};
    camera.lookAt({0, 2, 0});
    const glm::vec3 viewPos = glm::vec3(camera.viewMatrix() * glm::vec4(0, 2, 0, 1));
    CHECK(viewPos.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(viewPos.z == doctest::Approx(-5.0f));
    CHECK(camera.forward().z == doctest::Approx(-1.0f));

    camera.setYawPitch(glm::half_pi<float>(), 0.0f); // Turn left: look along -X.
    CHECK(camera.forward().x == doctest::Approx(-1.0f));
    camera.setYawPitch(0.0f, glm::radians(30.0f));   // Look up.
    CHECK(camera.forward().y == doctest::Approx(0.5f));

    CHECK_THROWS(camera.lookAt(camera.position));
    CHECK_THROWS(perspectiveReverseZ(1.0f, 1.0f, 1.0f, 0.5f));
}
