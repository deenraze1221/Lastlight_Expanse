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
