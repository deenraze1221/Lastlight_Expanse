#pragma once

#include "Assets/ImageData.h"
#include "Scene/Voxel/ChunkMesher.h"

#include <cstdint>
#include <vector>

namespace lastlight {

// Procedural pixel-art block textures: hard-edged, palette-quantised, seamlessly tiling.
// Standard textures are 256x256, hero textures 512x512 (separate texture arrays, see
// shaders/voxel.frag). sRGB RGBA8 with full mip chains; alpha < 1 marks emissive pixels.
struct BlockTextureSet {
    std::vector<zodar::ImageData> standard; // 256x256 layers
    std::vector<zodar::ImageData> hero;     // 512x512 layers
};

constexpr uint32_t kStandardTextureSize = 256;
constexpr uint32_t kHeroTextureSize = 512;
constexpr uint16_t kHeroLayerBase = 1000; // palette layer >= this selects the hero array

BlockTextureSet generateBlockTextures(uint64_t seed);
// Assigns per-face layers to the palette (top/side/bottom) matching generateBlockTextures().
void assignFaceLayers(zodar::voxel::VoxelPalette& palette);

} // namespace lastlight
