#include "World/LastlightTerrain.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace lastlight {

using namespace zodar::voxel;

VoxelPalette makePalette() {
    // Linear colours.
    VoxelPalette p;
    p.colors.resize(Block::Count);
    p.colors[Air] = {0, 0, 0, 0};
    p.colors[Bedrock] = {0.03f, 0.03f, 0.035f, 1};
    p.colors[Stone] = {0.22f, 0.21f, 0.2f, 1};
    p.colors[Basalt] = {0.07f, 0.07f, 0.08f, 1};
    p.colors[Dirt] = {0.16f, 0.09f, 0.05f, 1};
    p.colors[Moss] = {0.12f, 0.2f, 0.06f, 1};
    p.colors[Sand] = {0.45f, 0.36f, 0.22f, 1};
    p.colors[Ash] = {0.25f, 0.24f, 0.25f, 1};
    p.colors[Snow] = {0.8f, 0.82f, 0.88f, 1};
    p.colors[Ember] = {1.0f, 0.3f, 0.05f, 1};
    return p;
}

LastlightTerrain::LastlightTerrain(uint64_t seed)
    : m_continent(seed), m_ridges(seed ^ 0xA5A5u), m_detail(seed ^ 0x5151u), m_caves(seed ^ 0xC0FEu), m_seed(seed) {}

int LastlightTerrain::surfaceHeight(int x, int z) const {
    const glm::vec2 p(static_cast<float>(x), static_cast<float>(z));
    const float continent = m_continent.fbm(p * 0.0025f, 4);                // -1..1
    const float mountainMask = std::clamp((continent - 0.05f) * 3.0f, 0.0f, 1.0f);
    const float ridges = m_ridges.ridged(p * 0.006f, 4);                    // 0..1
    const float detail = m_detail.fbm(p * 0.03f, 3);
    const float h = 8.0f + continent * 22.0f + mountainMask * ridges * 85.0f + detail * 3.0f;
    return std::clamp(static_cast<int>(std::floor(h)), -60, kMaxHeight);
}

void LastlightTerrain::generate(const ChunkCoord& coord, VoxelChunk& out) const {
    const glm::ivec3 origin = coord.originVoxel();
    std::array<int, kChunkSize * kChunkSize> heights{};
    int maxH = INT32_MIN;
    for (int z = 0; z < kChunkSize; ++z) {
        for (int x = 0; x < kChunkSize; ++x) {
            const int h = surfaceHeight(origin.x + x, origin.z + z);
            heights[z * kChunkSize + x] = h;
            maxH = std::max(maxH, h);
        }
    }
    if (origin.y > maxH) {
        return; // all air (uniform, no storage)
    }
    const bool cavesPossible = origin.y < maxH - 4;
    for (int y = 0; y < kChunkSize; ++y) {
        const int wy = origin.y + y;
        for (int z = 0; z < kChunkSize; ++z) {
            for (int x = 0; x < kChunkSize; ++x) {
                const int h = heights[z * kChunkSize + x];
                if (wy > h) {
                    continue;
                }
                const int depth = h - wy;
                Voxel v;
                if (wy <= -58) {
                    v = Bedrock;
                } else if (depth == 0) {
                    v = h > 70 ? Snow : h > 38 ? Stone : h < 2 ? Ash : h < 5 ? Sand : Moss;
                } else if (depth < 4) {
                    v = h > 38 ? Stone : h < 5 ? Sand : Dirt;
                } else {
                    v = (wy / 7 + depth) % 5 == 0 ? Basalt : Stone;
                }
                if (cavesPossible && depth > 4 && wy > -55) {
                    const glm::vec3 p(static_cast<float>(origin.x + x), static_cast<float>(wy) * 1.6f,
                                      static_cast<float>(origin.z + z));
                    const float cave = m_caves.fbm(p * 0.02f, 2);
                    if (std::abs(cave) < 0.06f) {
                        continue; // tunnel
                    }
                    if (std::abs(cave) < 0.075f && (zodar::hashCoords(origin.x + x, wy, origin.z + z, m_seed) & 15u) == 0) {
                        v = Ember;
                    }
                }
                out.set(x, y, z, v);
            }
        }
    }
}

} // namespace lastlight
