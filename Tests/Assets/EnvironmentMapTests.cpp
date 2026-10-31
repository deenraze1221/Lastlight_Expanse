#include <doctest/doctest.h>

#include "Assets/EnvironmentMaps.h"
#include "Core/Error.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace zodar;

namespace {
// Minimal Radiance file (flat RGBE scanlines; RLE is not used below width 8).
std::vector<uint8_t> radianceFile(uint32_t width, uint32_t height, const std::vector<float>& rgb) {
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(height) + " +X " +
                               std::to_string(width) + "\n";
    std::vector<uint8_t> out(header.begin(), header.end());
    for (size_t i = 0; i < rgb.size(); i += 3) {
        const float m = std::max({rgb[i], rgb[i + 1], rgb[i + 2]});
        int e = 0;
        const float scale = m > 1e-32f ? std::frexp(m, &e) * 256.0f / m : 0.0f;
        out.push_back(uint8_t(rgb[i] * scale));
        out.push_back(uint8_t(rgb[i + 1] * scale));
        out.push_back(uint8_t(rgb[i + 2] * scale));
        out.push_back(m > 1e-32f ? uint8_t(e + 128) : 0);
    }
    return out;
}

glm::vec4 texel(const ImageData& img, uint32_t x, uint32_t y) {
    const auto* h = reinterpret_cast<const uint16_t*>(img.levels[0].data()) + (size_t(y) * img.width + x) * 4;
    return {glm::unpackHalf1x16(h[0]), glm::unpackHalf1x16(h[1]), glm::unpackHalf1x16(h[2]), glm::unpackHalf1x16(h[3])};
}
} // namespace

TEST_CASE("environment maps: Radiance .hdr decodes to linear half floats above 1") {
    const std::vector<uint8_t> file = radianceFile(2, 1, {0.25f, 2.0f, 40.0f, 1.0f, 0.5f, 0.0f});
    const ImageData img = loadHdrImageFromMemory(file, "test.hdr");
    CHECK((img.format == PixelFormat::Rgba16f));
    CHECK(img.colorSpace == ColorSpace::Linear);
    CHECK(img.width == 2);
    CHECK(texel(img, 0, 0).r == doctest::Approx(0.25f).epsilon(0.02)); // RGBE precision
    CHECK(texel(img, 0, 0).g == doctest::Approx(2.0f).epsilon(0.02));
    CHECK(texel(img, 0, 0).b == doctest::Approx(40.0f).epsilon(0.02));
    CHECK(texel(img, 0, 0).a == 1.0f);
    CHECK_THROWS_AS(loadHdrImageFromMemory(std::vector<uint8_t>{1, 2, 3}, "bad.hdr"), Error);
}

TEST_CASE("environment maps: procedural sky and uniform environment") {
    ProceduralSkyDesc desc;
    desc.sunDirection = {0.0f, -1.0f, 0.0f}; // Sun at the zenith
    desc.sunAngularRadius = 0.1f;          // Covers the top texel row (centres at 0.049 rad)
    const ImageData sky = proceduralSky(64, desc);
    CHECK(sky.width == 64);
    CHECK(sky.height == 32);
    CHECK(texel(sky, 10, 0).r > 10.0f);            // Top row: the sun disk
    CHECK(texel(sky, 10, 31).r < texel(sky, 10, 12).r); // Ground darker than sky
    CHECK(texel(sky, 10, 12).b > texel(sky, 10, 12).r); // Blue sky
    const ImageData uniform = uniformEnvironment({0.5f, 1.0f, 2.0f}, 16);
    for (uint32_t y = 0; y < uniform.height; ++y) {
        CHECK(texel(uniform, 3, y) == glm::vec4(0.5f, 1.0f, 2.0f, 1.0f));
    }
}
