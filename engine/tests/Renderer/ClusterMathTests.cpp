#include "Renderer/Lighting/ClusterMath.h"
#include "Renderer/Lighting/ClusteredLighting.h"

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <bit>
#include <cmath>

using namespace zodar::gfx;

TEST_CASE("clusters: grid dimensions follow tile size and quality") {
    const ClusterGrid g = makeClusterGrid(1920, 1080, clusterConfig(LightingQuality::High), 0.05f, 500.0f);
    CHECK(g.x == 30);
    CHECK(g.y == 17); // 1080 / 64 = 16.875
    CHECK(g.z == 32);
    CHECK(g.count() == 30 * 17 * 32);
    const ClusterGrid low = makeClusterGrid(1920, 1080, clusterConfig(LightingQuality::Low), 0.05f, 500.0f);
    CHECK(low.count() < g.count());
    const ClusterGrid ultra = makeClusterGrid(1920, 1080, clusterConfig(LightingQuality::Ultra), 0.05f, 500.0f);
    CHECK(ultra.count() > g.count());
    for (LightingQuality q : {LightingQuality::Low, LightingQuality::Medium, LightingQuality::High, LightingQuality::Ultra}) {
        CHECK(clusterConfig(q).maxLightsPerCluster % 2 == 0); // 16-bit index pairs
    }
    // Tiles scale with the output height above 1080p: cluster counts stay similar.
    const ClusterGrid uhd = makeClusterGrid(3840, 2160, clusterConfig(LightingQuality::High), 0.05f, 500.0f);
    CHECK(uhd.tileSize == 128);
    CHECK(uhd.x == g.x);
    CHECK(uhd.y == g.y);
    CHECK(makeClusterGrid(640, 360, clusterConfig(LightingQuality::High), 0.05f, 500.0f).tileSize == 64);
}

TEST_CASE("clusters: exponential slices are monotonic and match slice depths") {
    const ClusterGrid g = makeClusterGrid(1280, 720, clusterConfig(LightingQuality::High), 0.1f, 300.0f);
    CHECK(sliceDepth(g, 0) == doctest::Approx(0.1f));
    CHECK(sliceDepth(g, g.z) == doctest::Approx(300.0f));
    uint32_t previous = 0;
    for (float d = 0.01f; d < 1000.0f; d *= 1.07f) {
        const uint32_t s = clusterSlice(g, d);
        CHECK(s >= previous);
        CHECK(s < g.z);
        previous = s;
    }
    for (uint32_t s = 0; s < g.z; ++s) {
        // A depth inside slice s maps back to s.
        const float mid = std::sqrt(sliceDepth(g, s) * sliceDepth(g, s + 1));
        CHECK(clusterSlice(g, mid) == s);
    }
    CHECK(clusterSlice(g, 0.001f) == 0);        // In front of the near plane
    CHECK(clusterSlice(g, 1e6f) == g.z - 1);    // Beyond the slices
}

TEST_CASE("lights: effective ranges and spot cone parameters") {
    CHECK(effectiveLightRange(20.0f, glm::vec3(1.0f), 7.0f, 0.02f) == 7.0f); // Explicit range wins.
    // Automatic: irradiance intensity * max(color) / d^2 reaches the cutoff at the range.
    const float r = effectiveLightRange(20.0f, glm::vec3(0.5f, 1.0f, 0.2f), 0.0f, 0.02f);
    CHECK(20.0f * 1.0f / (r * r) == doctest::Approx(0.02f));
    CHECK(effectiveLightRange(0.0f, glm::vec3(1.0f), 0.0f, 0.02f) == 0.0f);

    const glm::vec2 so = spotScaleOffset(0.3f, 0.5f);
    const auto att = [&](float angle) {
        const float a = std::clamp(std::cos(angle) * so.x + so.y, 0.0f, 1.0f);
        return a * a;
    };
    CHECK(att(0.0f) == doctest::Approx(1.0f));
    CHECK(att(0.3f) == doctest::Approx(1.0f));
    CHECK(att(0.5f) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(att(0.7f) == 0.0f);
    CHECK(att(0.4f) > 0.0f);
    CHECK(att(0.4f) < 1.0f);
}

TEST_CASE("lights: GPU light conversion (points first, spots, view space, limits)") {
    SceneLighting lighting;
    lighting.pointLights = {{{1, 2, 3}, {1, 0.5f, 0.25f}, 8.0f, 4.0f}};
    SpotLight spot;
    spot.position = {0, 5, 0};
    spot.direction = {0, -2, 0};
    lighting.spotLights = {spot};
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0), glm::vec3(0, 1, 0));
    const auto lights = buildGpuLights(lighting, view, 0.02f, 100);
    REQUIRE(lights.size() == 2);
    CHECK(std::bit_cast<uint32_t>(lights[0].colorType.a) == 0u);
    CHECK(std::bit_cast<uint32_t>(lights[1].colorType.a) == 1u);
    CHECK(lights[0].positionRange.w == 4.0f);
    CHECK(lights[0].colorType.r == 8.0f);
    CHECK(lights[0].colorType.g == 4.0f);
    CHECK(lights[0].viewPositionRange.z == doctest::Approx(-7.0f)); // 3 - 10
    CHECK(lights[1].directionSpot.y == doctest::Approx(-1.0f));     // Normalised
    CHECK(glm::length(glm::vec3(lights[1].viewDirection)) == doctest::Approx(1.0f));
    CHECK(lights[1].spotOffsetCone.y == doctest::Approx(std::cos(spot.outerConeRadians)));
    CHECK(buildGpuLights(lighting, view, 0.02f, 1).size() == 1); // maxLights
}
