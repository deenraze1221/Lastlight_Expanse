#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace raven {

// How texel values are interpreted; decides the GPU format and correct mip filtering.
enum class ColorSpace {
    Srgb,   // Colour data authored in sRGB (albedo, emissive, UI).
    Linear, // Data (roughness/metallic/occlusion masks, linear colour).
};

enum class MipFilter {
    Box,       // 2x2 average in the image's colour space (sRGB is linearised first).
    NormalMap, // Decode [-1,1] vectors, average, renormalise.
};

// 8-bit RGBA image with an optional mip chain (levels[0] is the full image).
struct ImageData {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    ColorSpace colorSpace = ColorSpace::Srgb;
    std::vector<std::vector<uint8_t>> levels;

    uint32_t levelWidth(size_t level) const { return std::max(1u, width >> level); }
    uint32_t levelHeight(size_t level) const { return std::max(1u, height >> level); }

    // Replaces levels[1..] with a full chain down to 1x1.
    void generateMips(MipFilter filter = MipFilter::Box);

    static ImageData solid(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                           ColorSpace colorSpace, std::string name = {});
};

// Decodes PNG, JPEG, TGA or BMP into RGBA8 (single level). Throws raven::Error with the
// file name and decoder reason on failure.
ImageData loadImage(const std::filesystem::path& path, ColorSpace colorSpace);
ImageData loadImageFromMemory(std::span<const uint8_t> bytes, ColorSpace colorSpace, const std::string& name);

} // namespace raven
