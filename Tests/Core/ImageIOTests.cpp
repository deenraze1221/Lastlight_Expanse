#include <doctest/doctest.h>

#include "Core/ImageIO.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

TEST_CASE("imageio: PPM header and RGB payload") {
    const auto path = std::filesystem::temp_directory_path() / "raven_image_test.ppm";
    const std::vector<uint8_t> rgba = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 10, 20, 30, 40};
    REQUIRE(raven::writePpm(path, 2, 2, rgba));

    std::ifstream file(path, std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string header = "P6\n2 2\n255\n";
    REQUIRE(data.size() == header.size() + 12);
    CHECK(data.substr(0, header.size()) == header);
    CHECK(static_cast<uint8_t>(data[header.size() + 0]) == 255);
    CHECK(static_cast<uint8_t>(data[header.size() + 4]) == 255);
    CHECK(static_cast<uint8_t>(data[header.size() + 11]) == 30);
    std::filesystem::remove(path);
}

TEST_CASE("imageio: size mismatch is rejected") {
    const std::vector<uint8_t> rgba(12);
    CHECK_FALSE(raven::writePpm(std::filesystem::temp_directory_path() / "x.ppm", 2, 2, rgba));
    CHECK_FALSE(raven::writePpm(std::filesystem::temp_directory_path() / "x.ppm", 0, 0, {}));
}
