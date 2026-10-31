#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace zodar {

// How texel values are interpreted; decides the GPU format and correct mip filtering.
enum class ColorSpace {
    Srgb,   // Colour data authored in sRGB (albedo, emissive, UI).
    Linear, // Data (roughness/metallic/occlusion masks, linear colour).
};

// Texel encoding of ImageData::levels. Block formats store rows of 4x4 blocks.
enum class PixelFormat : uint8_t {
    Rgba8, // 4 bytes per texel
    Bc1,   // RGB + 1-bit alpha, 8 bytes per block
    Bc3,   // RGBA, 16 bytes per block
    Bc4,   // R, 8 bytes per block
    Bc5,   // RG, 16 bytes per block
    Bc7,   // RGBA, 16 bytes per block
    Rgba16f, // HDR: 4 half floats (8 bytes) per texel, linear
};

bool isBlockCompressed(PixelFormat format);
bool isFloatFormat(PixelFormat format);
// Bytes of one tightly packed level (whole 4x4 blocks for BCn).
size_t pixelFormatLevelSize(PixelFormat format, uint32_t width, uint32_t height);
const char* toString(PixelFormat format);

enum class MipFilter {
    Box,       // 2x2 average in the image's colour space (sRGB is linearised first).
    NormalMap, // Decode [-1,1] vectors, average, renormalise.
};

// Image with an optional mip chain (levels[0] is the full image): RGBA8 from decoders, or
// block-compressed data from compiled KTX2 assets.
struct ImageData {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    ColorSpace colorSpace = ColorSpace::Srgb;
    PixelFormat format = PixelFormat::Rgba8;
    std::vector<std::vector<uint8_t>> levels;

    uint32_t levelWidth(size_t level) const { return std::max(1u, width >> level); }
    uint32_t levelHeight(size_t level) const { return std::max(1u, height >> level); }

    // Replaces levels[1..] with a full chain down to 1x1 (RGBA8 only). Compiled assets have
    // baked mips (ZodarAssetCompiler); this is the offline tool's filter and the runtime
    // fallback for uncompiled source images.
    void generateMips(MipFilter filter = MipFilter::Box);
    uint32_t fullMipCount() const;
    size_t byteSize() const;

    static ImageData solid(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                           ColorSpace colorSpace, std::string name = {});
};

// Decodes block-compressed levels into RGBA8 (bcdec). Fallback for devices without BCn
// sampling, and for tests. RGBA8 input is returned unchanged.
ImageData decompressToRgba8(const ImageData& image);

// Decodes PNG, JPEG, TGA or BMP into RGBA8 (single level). Throws zodar::Error with the
// file name and decoder reason on failure.
ImageData loadImage(const std::filesystem::path& path, ColorSpace colorSpace);
ImageData loadImageFromMemory(std::span<const uint8_t> bytes, ColorSpace colorSpace, const std::string& name);

// Decodes a Radiance .hdr (RGBE) file into linear Rgba16f (single level). Throws on failure.
ImageData loadHdrImage(const std::filesystem::path& path);
ImageData loadHdrImageFromMemory(std::span<const uint8_t> bytes, const std::string& name);
// Linear float RGB(A) pixels -> Rgba16f.
ImageData makeRgba16f(uint32_t width, uint32_t height, std::span<const float> rgba, std::string name);

} // namespace zodar
