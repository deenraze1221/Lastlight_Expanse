#include "Core/ImageIO.h"

#include <fstream>
#include <string>
#include <vector>

namespace raven {

bool writePpm(const std::filesystem::path& path, uint32_t width, uint32_t height,
              std::span<const uint8_t> rgba) {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (width == 0 || height == 0 || rgba.size() != pixelCount * 4) {
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    const std::string header = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    file.write(header.data(), static_cast<std::streamsize>(header.size()));

    std::vector<uint8_t> rgb(pixelCount * 3);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    file.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return static_cast<bool>(file);
}

} // namespace raven
