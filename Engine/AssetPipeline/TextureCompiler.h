#pragma once

#include "Assets/ImageData.h"
#include "Assets/TextureCache.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace zodar::assets {

// Offline texture compilation: RGBA8 source -> baked mip chain -> BCn (or RGBA8) -> KTX2.
// Not linked by the runtime renderer; used by ZodarAssetCompiler and tests.

enum class EncodeQuality : uint8_t { Fast, Normal, High };

struct TextureCompileOptions {
    PixelFormat format = PixelFormat::Bc7;
    ColorSpace colorSpace = ColorSpace::Srgb;
    MipFilter mipFilter = MipFilter::Box;
    bool mips = true;
    EncodeQuality quality = EncodeQuality::Normal;
    uint32_t threads = 0; // 0: hardware concurrency
};

// Default options for a model texture usage (BC7; sRGB for colour).
TextureCompileOptions optionsFor(TextureUsage usage);

// Encodes every level of an RGBA8 image into `format` (block-parallel, deterministic: the
// output does not depend on the thread count). Edge blocks replicate the last row/column.
ImageData encodeImage(const ImageData& rgba8, PixelFormat format, EncodeQuality quality, uint32_t threads = 0);

// Baked mips (ImageData::generateMips with the usage's filter) + encoding.
ImageData compileTexture(const ImageData& source, const TextureCompileOptions& options);

// Peak signal-to-noise ratio in dB over the first `channels` channels of two RGBA8 levels.
double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t channels = 4);

// Key/value entries the compiler writes into every KTX2 file.
std::map<std::string, std::string> compilerKeyValues(const TextureCompileOptions& options);

PixelFormat parsePixelFormat(const std::string& name);

} // namespace zodar::assets
