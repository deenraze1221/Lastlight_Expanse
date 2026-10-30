#include "Assets/ImageData.h"

#include "Core/Error.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include <stb_image.h>

namespace zodar {
namespace {

float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

const std::array<float, 256>& srgbTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            t[static_cast<size_t>(i)] = srgbToLinear(static_cast<float>(i) / 255.0f);
        }
        return t;
    }();
    return table;
}

uint8_t toByte(float v) {
    return static_cast<uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
}

} // namespace

void ImageData::generateMips(MipFilter filter) {
    ZODAR_ENSURE(!levels.empty() && levels[0].size() == static_cast<size_t>(width) * height * 4,
                 "generateMips: image '" + name + "' has no valid base level");
    levels.resize(1);
    const auto& lut = srgbTable();
    const bool srgb = colorSpace == ColorSpace::Srgb && filter == MipFilter::Box;

    while (levelWidth(levels.size() - 1) > 1 || levelHeight(levels.size() - 1) > 1) {
        const size_t srcLevel = levels.size() - 1;
        const uint32_t sw = levelWidth(srcLevel), sh = levelHeight(srcLevel);
        const uint32_t dw = std::max(1u, sw / 2), dh = std::max(1u, sh / 2);
        const std::vector<uint8_t>& src = levels[srcLevel];
        std::vector<uint8_t> dst(static_cast<size_t>(dw) * dh * 4);

        for (uint32_t y = 0; y < dh; ++y) {
            for (uint32_t x = 0; x < dw; ++x) {
                float acc[4] = {0, 0, 0, 0};
                // 2x2 footprint, clamped so 1-pixel-wide dimensions still work.
                for (uint32_t oy = 0; oy < 2; ++oy) {
                    for (uint32_t ox = 0; ox < 2; ++ox) {
                        const uint32_t sx = std::min(x * 2 + ox, sw - 1);
                        const uint32_t sy = std::min(y * 2 + oy, sh - 1);
                        const uint8_t* p = &src[(static_cast<size_t>(sy) * sw + sx) * 4];
                        for (int c = 0; c < 3; ++c) {
                            if (srgb) {
                                acc[c] += lut[p[c]];
                            } else if (filter == MipFilter::NormalMap) {
                                acc[c] += p[c] / 255.0f * 2.0f - 1.0f;
                            } else {
                                acc[c] += p[c] / 255.0f;
                            }
                        }
                        acc[3] += p[3] / 255.0f;
                    }
                }
                uint8_t* out = &dst[(static_cast<size_t>(y) * dw + x) * 4];
                if (filter == MipFilter::NormalMap) {
                    float len = std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
                    if (len < 1e-8f) {
                        acc[0] = acc[1] = 0.0f;
                        acc[2] = len = 1.0f;
                    }
                    for (int c = 0; c < 3; ++c) {
                        out[c] = toByte((acc[c] / len) * 0.5f + 0.5f);
                    }
                } else {
                    for (int c = 0; c < 3; ++c) {
                        const float v = acc[c] * 0.25f;
                        out[c] = toByte(srgb ? linearToSrgb(v) : v);
                    }
                }
                out[3] = toByte(acc[3] * 0.25f);
            }
        }
        levels.push_back(std::move(dst));
    }
}

ImageData ImageData::solid(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                           ColorSpace colorSpace, std::string name) {
    ImageData image;
    image.name = std::move(name);
    image.width = width;
    image.height = height;
    image.colorSpace = colorSpace;
    image.levels.emplace_back(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < image.levels[0].size(); i += 4) {
        image.levels[0][i] = r;
        image.levels[0][i + 1] = g;
        image.levels[0][i + 2] = b;
        image.levels[0][i + 3] = a;
    }
    return image;
}

ImageData loadImageFromMemory(std::span<const uint8_t> bytes, ColorSpace colorSpace, const std::string& name) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
        fail("Cannot decode image '" + name + "': " + stbi_failure_reason());
    }
    ImageData image;
    image.name = name;
    image.width = static_cast<uint32_t>(w);
    image.height = static_cast<uint32_t>(h);
    image.colorSpace = colorSpace;
    image.levels.emplace_back(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return image;
}

ImageData loadImage(const std::filesystem::path& path, ColorSpace colorSpace) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        fail("Cannot open image '" + path.string() + "'");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return loadImageFromMemory(bytes, colorSpace, path.string());
}

} // namespace zodar
