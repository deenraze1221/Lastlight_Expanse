#include <doctest/doctest.h>

#include "Core/ImageIO.h"
#include "../Support/TempFile.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

TEST_CASE("imageio: PPM header and RGB payload") {
    const zodar::test::TempFile image("zodar_image_test.ppm");
    const std::vector<uint8_t> rgba = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 10, 20, 30, 40};
    REQUIRE(zodar::writePpm(image.path(), 2, 2, rgba));

    const std::string data = zodar::test::readFile(image.path());
    const std::string header = "P6\n2 2\n255\n";
    REQUIRE(data.size() == header.size() + 12);
    CHECK(data.substr(0, header.size()) == header);
    CHECK(static_cast<uint8_t>(data[header.size() + 0]) == 255);
    CHECK(static_cast<uint8_t>(data[header.size() + 4]) == 255);
    CHECK(static_cast<uint8_t>(data[header.size() + 11]) == 30);
    CHECK(image.remove()); // writePpm closed its stream; nothing may hold the file open.
}

TEST_CASE("imageio: size mismatch is rejected") {
    const zodar::test::TempFile image("zodar_image_rejected.ppm");
    const std::vector<uint8_t> rgba(12);
    CHECK_FALSE(zodar::writePpm(image.path(), 2, 2, rgba));
    CHECK_FALSE(zodar::writePpm(image.path(), 0, 0, {}));
    CHECK_FALSE(std::filesystem::exists(image.path())); // Rejected before creating a file.
}

TEST_CASE("imageio: PNG signature, size and extension dispatch") {
    const zodar::test::TempFile png("zodar_image_test.png");
    const zodar::test::TempFile upper("zodar_image_test_upper.PNG");
    const zodar::test::TempFile ppm("zodar_image_dispatch.ppm");
    const std::vector<uint8_t> rgba = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 10, 20, 30, 40};
    REQUIRE(zodar::writePng(png.path(), 2, 2, rgba));
    const std::string data = zodar::test::readFile(png.path());
    REQUIRE(data.size() > 33);
    CHECK(data.substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8));
    // IHDR: width and height big-endian at offset 16, bit depth 8, colour type 2 (RGB).
    CHECK(static_cast<uint8_t>(data[19]) == 2);
    CHECK(static_cast<uint8_t>(data[23]) == 2);
    CHECK(static_cast<uint8_t>(data[24]) == 8);
    CHECK(static_cast<uint8_t>(data[25]) == 2);

    REQUIRE(zodar::writeImage(upper.path(), 2, 2, rgba));
    CHECK(zodar::test::readFile(upper.path()).substr(1, 3) == "PNG");
    REQUIRE(zodar::writeImage(ppm.path(), 2, 2, rgba));
    CHECK(zodar::test::readFile(ppm.path()).substr(0, 2) == "P6");

    CHECK_FALSE(zodar::writePng(png.path(), 3, 2, rgba)); // size mismatch
    CHECK_FALSE(zodar::writeImage("zodar_image_test.jpg", 2, 2, rgba)); // unsupported extension
    CHECK_FALSE(std::filesystem::exists("zodar_image_test.jpg"));
    CHECK(png.remove());
}
