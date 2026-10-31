#include "Rendering/BlockTextures.h"

#include "Scene/Noise.h"
#include "World/LastlightTerrain.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>

namespace lastlight {
namespace {

using zodar::ImageData;

// Layer indices of the standard array.
enum StandardLayer : uint16_t {
    LStone, LBasalt, LDirt, LMossTop, LMossSide, LSand, LAsh, LSnowTop, LSnowSide, LBedrock, LCount
};
enum HeroLayer : uint16_t { HEmber };

// --- Tileable noise ------------------------------------------------------------------

float hashUnit(int x, int y, uint64_t seed) {
    return static_cast<float>(zodar::hashCoords(x, y, 0, seed) >> 40) / static_cast<float>(1u << 24);
}

// Value noise with a lattice of `period` cells over [0, 1)^2, wrapping at the edges.
float tileNoise(float u, float v, int period, uint64_t seed) {
    const float x = u * period;
    const float y = v * period;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - x0;
    const float fy = y - y0;
    const auto at = [&](int i, int j) { return hashUnit(((i % period) + period) % period, ((j % period) + period) % period, seed); };
    const float sx = fx * fx * (3 - 2 * fx);
    const float sy = fy * fy * (3 - 2 * fy);
    const float a = glm::mix(at(x0, y0), at(x0 + 1, y0), sx);
    const float b = glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), sx);
    return glm::mix(a, b, sy);
}

float tileFbm(float u, float v, int basePeriod, int octaves, uint64_t seed) {
    float sum = 0, amp = 1, norm = 0;
    int period = basePeriod;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * tileNoise(u, v, period, seed + i * 7919);
        norm += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / norm;
}

// Tileable Voronoi: distance to nearest and second-nearest feature point.
glm::vec2 tileVoronoi(float u, float v, int cells, uint64_t seed) {
    const float x = u * cells;
    const float y = v * cells;
    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));
    float d1 = 1e9f, d2 = 1e9f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int gx = cx + i, gy = cy + j;
            const int wx = ((gx % cells) + cells) % cells, wy = ((gy % cells) + cells) % cells;
            const glm::vec2 pt(gx + hashUnit(wx, wy, seed), gy + hashUnit(wx, wy, seed ^ 0x9E37u));
            const float d = glm::length(pt - glm::vec2(x, y));
            if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) { d2 = d; }
        }
    }
    return {d1, d2};
}

// --- Pixel-art helpers -----------------------------------------------------------------

using Palette = std::vector<glm::vec3>; // sRGB 0..255, dark to light

glm::vec3 quantize(const Palette& p, float t) {
    const int i = std::clamp(static_cast<int>(t * static_cast<float>(p.size())), 0, static_cast<int>(p.size()) - 1);
    return p[i];
}

// Generates one texture; `pixel` returns sRGB 0..255 colour and alpha (1 = not emissive)
// for texel coordinates in [0, 1).
ImageData makeTexture(const std::string& name, uint32_t size,
                      const std::function<glm::vec4(float u, float v, int x, int y)>& pixel) {
    ImageData img;
    img.name = name;
    img.width = img.height = size;
    img.format = zodar::PixelFormat::Rgba8;
    img.colorSpace = zodar::ColorSpace::Srgb;
    img.levels.resize(1);
    auto& texels = img.levels[0];
    texels.resize(static_cast<size_t>(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const glm::vec4 c = pixel((x + 0.5f) / size, (y + 0.5f) / size, static_cast<int>(x), static_cast<int>(y));
            uint8_t* t = &texels[(static_cast<size_t>(y) * size + x) * 4];
            t[0] = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 255.0f));
            t[1] = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 255.0f));
            t[2] = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 255.0f));
            t[3] = static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f));
        }
    }
    img.generateMips();
    return img;
}

// Art pixels are 2x2 texels: crisp, readable detail at 256x256 (4x4 for hero textures keeps
// the same art density at 512x512 with finer highlights).
glm::vec2 snap(float u, float v, uint32_t size, int artPixel) {
    const float cells = static_cast<float>(size) / static_cast<float>(artPixel);
    return {(std::floor(u * cells) + 0.5f) / cells, (std::floor(v * cells) + 0.5f) / cells};
}

const Palette kStone{{58, 56, 60}, {78, 76, 80}, {96, 94, 97}, {114, 111, 113}, {134, 130, 131}, {152, 148, 147}};
const Palette kBasalt{{22, 22, 27}, {32, 32, 38}, {44, 43, 50}, {56, 55, 62}, {70, 68, 76}};
const Palette kDirt{{58, 38, 26}, {76, 50, 33}, {94, 63, 41}, {112, 77, 50}, {128, 90, 60}};
const Palette kMoss{{38, 62, 24}, {52, 82, 30}, {66, 102, 36}, {84, 122, 44}, {104, 140, 54}, {128, 160, 70}};
const Palette kSand{{170, 140, 96}, {186, 156, 108}, {200, 170, 120}, {214, 186, 136}, {226, 200, 152}};
const Palette kAsh{{62, 60, 64}, {78, 76, 80}, {94, 92, 96}, {110, 108, 112}, {128, 126, 130}};
const Palette kSnow{{176, 190, 214}, {196, 208, 228}, {214, 224, 238}, {230, 238, 246}, {246, 250, 255}};
const Palette kBedrock{{8, 8, 10}, {20, 20, 24}, {40, 38, 44}, {70, 66, 72}};
const Palette kEmber{{120, 24, 6}, {190, 52, 10}, {240, 104, 20}, {255, 170, 60}, {255, 228, 140}};

glm::vec4 stonePixel(float u, float v, uint64_t seed) {
    const glm::vec2 s = snap(u, v, kStandardTextureSize, 2);
    float t = tileFbm(s.x, s.y, 4, 4, seed);
    const glm::vec2 vor = tileVoronoi(s.x, s.y, 5, seed + 1);
    if (vor.y - vor.x < 0.06f) t -= 0.35f;      // cracks between slabs
    else if (vor.y - vor.x < 0.12f) t += 0.12f; // lit crack edge
    return {quantize(kStone, t), 1.0f};
}

glm::vec4 dirtPixel(float u, float v, uint64_t seed) {
    const glm::vec2 s = snap(u, v, kStandardTextureSize, 2);
    float t = tileFbm(s.x, s.y, 8, 3, seed);
    const glm::vec2 peb = tileVoronoi(s.x, s.y, 14, seed + 3);
    if (peb.x < 0.18f) t = 0.85f - peb.x * 2.0f; // pebbles with a highlight
    return {quantize(kDirt, t), 1.0f};
}

glm::vec4 mossTopPixel(float u, float v, uint64_t seed) {
    const glm::vec2 s = snap(u, v, kStandardTextureSize, 2);
    float t = tileFbm(s.x, s.y, 8, 3, seed) * 0.7f + tileNoise(s.x, s.y * 0.25f, 64, seed + 5) * 0.4f;
    if (hashUnit(int(s.x * 128), int(s.y * 128), seed + 9) > 0.93f) t += 0.3f; // blade tips
    return {quantize(kMoss, t - 0.1f), 1.0f};
}

} // namespace

BlockTextureSet generateBlockTextures(uint64_t seed) {
    const uint32_t S = kStandardTextureSize;
    BlockTextureSet set;
    set.standard.resize(LCount);
    set.standard[LStone] = makeTexture("stone", S, [&](float u, float v, int, int) { return stonePixel(u, v, seed); });
    set.standard[LBasalt] = makeTexture("basalt", S, [&](float u, float v, int, int) {
        const glm::vec2 s = snap(u, v, S, 2);
        const glm::vec2 col = tileVoronoi(s.x, s.y, 4, seed + 11); // columnar joints
        float t = 0.35f + tileFbm(s.x, s.y, 8, 2, seed + 12) * 0.4f - col.x * 0.25f;
        if (col.y - col.x < 0.05f) t = 0.0f;
        return glm::vec4(quantize(kBasalt, t), 1.0f);
    });
    set.standard[LDirt] = makeTexture("dirt", S, [&](float u, float v, int, int) { return dirtPixel(u, v, seed + 20); });
    set.standard[LMossTop] = makeTexture("moss_top", S, [&](float u, float v, int, int) { return mossTopPixel(u, v, seed + 30); });
    set.standard[LMossSide] = makeTexture("moss_side", S, [&](float u, float v, int, int) {
        const glm::vec2 s = snap(u, v, S, 2);
        // Grass overhang with a jagged edge; v = 0 is the top of the block.
        const float edge = 0.16f + tileNoise(s.x, 0.5f, 32, seed + 31) * 0.14f;
        return s.y < edge ? mossTopPixel(u, v, seed + 30) : dirtPixel(u, v, seed + 20);
    });
    set.standard[LSand] = makeTexture("sand", S, [&](float u, float v, int, int) {
        const glm::vec2 s = snap(u, v, S, 2);
        const float ripple = std::sin((s.y + tileNoise(s.x, s.y, 4, seed + 41) * 0.25f) * 6.2831853f * 6.0f);
        const float t = 0.5f + ripple * 0.18f + (hashUnit(int(s.x * 128), int(s.y * 128), seed + 42) - 0.5f) * 0.35f;
        return glm::vec4(quantize(kSand, t), 1.0f);
    });
    set.standard[LAsh] = makeTexture("ash", S, [&](float u, float v, int, int) {
        const glm::vec2 s = snap(u, v, S, 2);
        float t = tileFbm(s.x, s.y, 4, 4, seed + 50);
        const float speck = hashUnit(int(s.x * 128), int(s.y * 128), seed + 51);
        if (speck > 0.97f) return glm::vec4(quantize(kEmber, 0.3f), 0.4f); // smouldering flecks
        if (speck < 0.06f) t -= 0.3f;
        return glm::vec4(quantize(kAsh, t), 1.0f);
    });
    const auto snowPixel = [&](float u, float v) {
        const glm::vec2 s = snap(u, v, S, 2);
        float t = 0.55f + (tileFbm(s.x, s.y, 4, 3, seed + 60) - 0.5f) * 0.8f;
        if (hashUnit(int(s.x * 128), int(s.y * 128), seed + 61) > 0.985f) t = 1.0f; // sparkle
        return glm::vec4(quantize(kSnow, t), 1.0f);
    };
    set.standard[LSnowTop] = makeTexture("snow_top", S, [&](float u, float v, int, int) { return snowPixel(u, v); });
    set.standard[LSnowSide] = makeTexture("snow_side", S, [&](float u, float v, int, int) {
        const glm::vec2 s = snap(u, v, S, 2);
        const float edge = 0.22f + tileNoise(s.x, 0.5f, 16, seed + 62) * 0.18f;
        return s.y < edge ? snowPixel(u, v) : stonePixel(u, v, seed + 70);
    });
    set.standard[LBedrock] = makeTexture("bedrock", S, [&](float u, float v, int, int) {
        const glm::vec2 s = snap(u, v, S, 4);
        return glm::vec4(quantize(kBedrock, tileFbm(s.x, s.y, 8, 2, seed + 80) * 1.2f - 0.1f), 1.0f);
    });

    // Hero asset (512x512): basalt split by glowing ember veins.
    set.hero.resize(1);
    set.hero[HEmber] = makeTexture("ember_hero", kHeroTextureSize, [&](float u, float v, int, int) {
        const glm::vec2 s = snap(u, v, kHeroTextureSize, 2);
        const glm::vec2 vein = tileVoronoi(s.x, s.y, 6, seed + 90);
        const float gap = vein.y - vein.x + (tileNoise(s.x, s.y, 16, seed + 91) - 0.5f) * 0.05f;
        if (gap < 0.05f) {
            const float heat = 1.0f - gap / 0.05f;
            return glm::vec4(quantize(kEmber, 0.25f + heat * 0.75f), 0.0f); // fully emissive core
        }
        if (gap < 0.08f) {
            return glm::vec4(quantize(kEmber, 0.1f), 0.6f); // glowing rim
        }
        const float t = 0.3f + tileFbm(s.x, s.y, 8, 3, seed + 92) * 0.5f - vein.x * 0.2f;
        return glm::vec4(quantize(kBasalt, t), 1.0f);
    });
    return set;
}

void assignFaceLayers(zodar::voxel::VoxelPalette& palette) {
    palette.faceLayers.assign(Block::Count, {LStone, LStone, LStone});
    palette.faceLayers[Bedrock] = {LBedrock, LBedrock, LBedrock};
    palette.faceLayers[Stone] = {LStone, LStone, LStone};
    palette.faceLayers[Basalt] = {LBasalt, LBasalt, LBasalt};
    palette.faceLayers[Dirt] = {LDirt, LDirt, LDirt};
    palette.faceLayers[Moss] = {LMossTop, LMossSide, LDirt};
    palette.faceLayers[Sand] = {LSand, LSand, LSand};
    palette.faceLayers[Ash] = {LAsh, LAsh, LAsh};
    palette.faceLayers[Snow] = {LSnowTop, LSnowSide, LStone};
    const uint16_t ember = static_cast<uint16_t>(kHeroLayerBase + HEmber);
    palette.faceLayers[Ember] = {ember, ember, ember};
    // Textures carry the colour; the palette keeps a neutral tint (AO is multiplied in).
    for (auto& c : palette.colors) {
        c = glm::vec4(1.0f);
    }
}

} // namespace lastlight
