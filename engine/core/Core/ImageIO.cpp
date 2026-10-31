#include "Core/ImageIO.h"

// Static: tests and tools define their own stb_image_write implementation.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

namespace zodar {

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

namespace {

std::vector<uint8_t> toRgb(std::span<const uint8_t> rgba, size_t pixelCount) {
    std::vector<uint8_t> rgb(pixelCount * 3);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    return rgb;
}

void appendBytes(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

bool writePng(const std::filesystem::path& path, uint32_t width, uint32_t height,
              std::span<const uint8_t> rgba) {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (width == 0 || height == 0 || rgba.size() != pixelCount * 4 || width > 1u << 24 || height > 1u << 24) {
        return false;
    }
    const std::vector<uint8_t> rgb = toRgb(rgba, pixelCount);
    std::vector<uint8_t> encoded;
    const int w = static_cast<int>(width);
    if (!stbi_write_png_to_func(appendBytes, &encoded, w, static_cast<int>(height), 3, rgb.data(), w * 3)) {
        return false;
    }
    // std::ofstream (not stbi_write_png) so non-ASCII paths work on Windows.
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    return static_cast<bool>(file);
}

bool writeImage(const std::filesystem::path& path, uint32_t width, uint32_t height,
                std::span<const uint8_t> rgba) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".png") return writePng(path, width, height, rgba);
    if (ext == ".ppm") return writePpm(path, width, height, rgba);
    return false;
}

} // namespace zodar
