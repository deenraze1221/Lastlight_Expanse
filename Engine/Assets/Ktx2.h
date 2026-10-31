#pragma once

#include "Assets/ImageData.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace zodar {

// KTX 2.0 container (https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html) for the
// engine's texture formats: 2D, one layer, one face, no supercompression, vkFormat one of
// R8G8B8A8 / BC1 RGBA / BC3 / BC4 / BC5 / BC7 (UNORM or SRGB). Levels are stored smallest
// first as the specification requires, so a streamer can read the mip tail first.
struct Ktx2File {
    ImageData image;
    std::map<std::string, std::string> keyValues; // Key/value data (e.g. "KTXwriter").
};

// Vulkan format numbers (from vulkan_core.h; Assets has no Vulkan dependency).
namespace ktx2_vk {
constexpr uint32_t R8G8B8A8_UNORM = 37, R8G8B8A8_SRGB = 43;
constexpr uint32_t BC1_RGBA_UNORM = 133, BC1_RGBA_SRGB = 134, BC3_UNORM = 137, BC3_SRGB = 138;
constexpr uint32_t BC4_UNORM = 139, BC5_UNORM = 141, BC7_UNORM = 145, BC7_SRGB = 146;
} // namespace ktx2_vk

uint32_t ktx2VkFormat(PixelFormat format, ColorSpace colorSpace);

// Serialises `image` (all its levels). BC4/BC5 must be ColorSpace::Linear.
std::vector<uint8_t> writeKtx2(const ImageData& image, const std::map<std::string, std::string>& keyValues = {});
void saveKtx2(const std::filesystem::path& path, const ImageData& image,
              const std::map<std::string, std::string>& keyValues = {});

// Parses and validates a KTX2 file. Throws zodar::Error naming the file and the problem for
// unsupported or corrupt files (bad identifier, offsets out of range, size mismatches).
Ktx2File readKtx2(std::span<const uint8_t> bytes, const std::string& name);
Ktx2File loadKtx2(const std::filesystem::path& path);

} // namespace zodar
