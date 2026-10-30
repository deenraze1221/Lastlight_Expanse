#include <doctest/doctest.h>

#include "Assets/ImageData.h"
#include "Core/Error.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#include <cmath>
#include <vector>

using namespace raven;

TEST_CASE("image: mip chain sizes for non-square, non-power-of-two images") {
    ImageData image = ImageData::solid(7, 3, 10, 20, 30, 255, ColorSpace::Linear);
    image.generateMips();
    REQUIRE(image.levels.size() == 3); // 7x3 -> 3x1 -> 1x1
    CHECK(image.levelWidth(1) == 3);
    CHECK(image.levelHeight(1) == 1);
    CHECK(image.levels[2].size() == 4);
    CHECK(image.levels[2][0] == 10); // A constant image stays constant.
}

TEST_CASE("image: sRGB mips average in linear space") {
    // Black and white checker: the linear average is 0.5, which is ~188 in sRGB, not 128.
    ImageData image;
    image.width = 2;
    image.height = 2;
    image.colorSpace = ColorSpace::Srgb;
    image.levels.push_back({0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255});
    image.generateMips();
    CHECK(image.levels[1][0] == 188);

    image.colorSpace = ColorSpace::Linear;
    image.generateMips();
    CHECK(image.levels[1][0] == 128);
}

TEST_CASE("image: normal map mips stay unit length") {
    ImageData image;
    image.width = 2;
    image.height = 1;
    image.colorSpace = ColorSpace::Linear;
    // Two normals tilted +X and -X: the average must renormalise to straight up (+Z).
    image.levels.push_back({218, 128, 218, 255, 37, 128, 218, 255});
    image.generateMips(MipFilter::NormalMap);
    const auto& n = image.levels[1];
    CHECK(std::abs(int(n[0]) - 128) <= 1);
    CHECK(std::abs(int(n[1]) - 128) <= 1);
    CHECK(n[2] == 255);
}

TEST_CASE("image: PNG decode and error reporting") {
    const uint8_t rgb[2 * 2 * 3] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    std::vector<uint8_t> png;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* out = static_cast<std::vector<uint8_t>*>(ctx);
            out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &png, 2, 2, 3, rgb, 6);
    const ImageData image = loadImageFromMemory(png, ColorSpace::Srgb, "mem.png");
    CHECK(image.width == 2);
    CHECK(image.levels[0].size() == 16);
    CHECK(image.levels[0][4 + 1] == 255); // Second pixel green, alpha expanded.
    CHECK(image.levels[0][3] == 255);

    const std::vector<uint8_t> garbage(64, 0xAB);
    CHECK_THROWS_AS(loadImageFromMemory(garbage, ColorSpace::Srgb, "garbage.png"), raven::Error);
    CHECK_THROWS_AS(loadImage("does/not/exist.png", ColorSpace::Srgb), raven::Error);
}
