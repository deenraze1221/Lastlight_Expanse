#pragma once

#include "Scene/Noise.h"
#include "Scene/Voxel/ChunkMesher.h"
#include "Scene/Voxel/VoxelWorld.h"

#include <cstdint>

namespace lastlight {

// Voxel ids of the Lastlight Expanse world (game-defined; the engine only knows 0 = empty).
enum Block : zodar::voxel::Voxel {
    Air = 0,
    Bedrock,
    Stone,
    Basalt,
    Dirt,
    Moss,
    Sand,
    Ash,
    Snow,
    Ember, // glowing veins in caves
    Count
};

zodar::voxel::VoxelPalette makePalette();

// Dusk-lit highlands: continental fBm plains, ridged mountain ranges, ash dunes in the
// lowlands, snow above the tree line and 3D-noise caves with ember veins.
// Thread-safe and deterministic per seed.
class LastlightTerrain final : public zodar::voxel::IVoxelGenerator {
public:
    explicit LastlightTerrain(uint64_t seed);
    void generate(const zodar::voxel::ChunkCoord& coord, zodar::voxel::VoxelChunk& out) const override;
    // Surface height (voxels) at a world column; used for spawn placement.
    int surfaceHeight(int x, int z) const;

    static constexpr int kSeaLevel = 0;
    static constexpr int kMaxHeight = 110;

private:
    zodar::GradientNoise m_continent;
    zodar::GradientNoise m_ridges;
    zodar::GradientNoise m_detail;
    zodar::GradientNoise m_caves;
    uint64_t m_seed;
};

} // namespace lastlight
