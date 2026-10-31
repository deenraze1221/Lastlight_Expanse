#pragma once

#include "Assets/ImageData.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace zodar {

// How a model texture is used; decides colour space, mip filter and compressed format.
enum class TextureUsage : uint8_t {
    Color,  // base colour, emissive: sRGB, box filter
    Data,   // metallic-roughness, occlusion: linear, box filter
    Normal, // tangent-space normal map: linear, normal-vector filter
};
ColorSpace colorSpaceFor(TextureUsage usage);
MipFilter mipFilterFor(TextureUsage usage);
const char* toString(TextureUsage usage);

// Compiled texture cache of a model (written by ZodarAssetCompiler, read by loadGltf):
//   <model dir>/<model file name>.zodar/image<index>_<usage>.ktx2
// Each file stores kTextureSourceKey = textureSourceKey(source bytes, usage). The loader
// uses a cached file only if the key matches, so edited source images are never shadowed
// by stale compiled data.
std::filesystem::path textureCacheDirectory(const std::filesystem::path& model);
std::filesystem::path textureCachePath(const std::filesystem::path& model, int imageIndex, TextureUsage usage);

inline constexpr const char* kTextureSourceKey = "zodar.source";
// Bump when the compiler's output for the same input changes (filters, encoder settings).
// 2: pixel-art textures compile losslessly (RGBA8 KTX2); v1 caches may hold lossy BC7 for them.
inline constexpr uint32_t kTextureCompilerVersion = 2;
std::string textureSourceKey(std::span<const uint8_t> sourceBytes, TextureUsage usage);

} // namespace zodar
