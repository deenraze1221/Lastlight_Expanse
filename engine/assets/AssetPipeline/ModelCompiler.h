#pragma once

#include "AssetPipeline/TextureCompiler.h"

#include <filesystem>
#include <string>
#include <vector>

namespace zodar::assets {

struct ModelCompileOptions {
    EncodeQuality quality = EncodeQuality::Normal;
    uint32_t threads = 0;
    bool force = false; // Recompile even if the cached file is up to date.
};

struct CompiledTexture {
    std::filesystem::path path;
    TextureUsage usage;
    PixelFormat format;
    uint32_t width, height, levels;
    size_t sourceBytes;   // Encoded source file
    size_t rgba8Bytes;    // Uncompressed RGBA8 with mips (what the runtime fallback uploads)
    size_t compiledBytes; // Texel data in the KTX2 file
    bool upToDate;        // Skipped: cache already matched
    double psnr;          // Level 0 vs. source (dB); 0 if skipped
    double seconds;
};

// Compiles every texture of a glTF/GLB model into its texture cache
// (textureCacheDirectory(model)): baked mips with the usage's filter, BC7, KTX2.
std::vector<CompiledTexture> compileModelTextures(const std::filesystem::path& model,
                                                  const ModelCompileOptions& options = {});

} // namespace zodar::assets
