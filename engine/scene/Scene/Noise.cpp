#include "Scene/Noise.h"

#include <cmath>
#include <utility>

namespace zodar {
namespace {

float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float lerp(float a, float b, float t) { return a + t * (b - a); }

float grad2(uint8_t h, float x, float y) {
    switch (h & 7u) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
    }
}

float grad3(uint8_t h, float x, float y, float z) {
    // The 12 cube-edge gradients of improved Perlin noise (4 repeated to fill 16).
    switch (h & 15u) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x + z;
    case 5: return -x + z;
    case 6: return x - z;
    case 7: return -x - z;
    case 8: return y + z;
    case 9: return -y + z;
    case 10: return y - z;
    case 11: return -y - z;
    case 12: return x + y;
    case 13: return -y + z;
    case 14: return -x + y;
    default: return -y - z;
    }
}

int floorInt(float v) {
    const int i = static_cast<int>(v);
    return v < static_cast<float>(i) ? i - 1 : i;
}

} // namespace

uint64_t hash64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

uint64_t hashCoords(int32_t x, int32_t y, int32_t z, uint64_t seed) {
    uint64_t h = hash64(seed);
    h = hash64(h ^ static_cast<uint32_t>(x));
    h = hash64(h ^ (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 1));
    return hash64(h ^ (static_cast<uint64_t>(static_cast<uint32_t>(z)) << 2));
}

GradientNoise::GradientNoise(uint64_t seed) : m_seed(seed) {
    std::array<uint8_t, 256> p{};
    for (int i = 0; i < 256; ++i) {
        p[i] = static_cast<uint8_t>(i);
    }
    uint64_t state = seed;
    for (int i = 255; i > 0; --i) { // Fisher-Yates with SplitMix64
        state = hash64(state);
        const int j = static_cast<int>(state % static_cast<uint64_t>(i + 1));
        std::swap(p[i], p[j]);
    }
    for (int i = 0; i < 512; ++i) {
        m_perm[i] = p[i & 255];
    }
}

float GradientNoise::sample(glm::vec2 p) const {
    const int xi = floorInt(p.x);
    const int yi = floorInt(p.y);
    const float x = p.x - static_cast<float>(xi);
    const float y = p.y - static_cast<float>(yi);
    const int X = xi & 255;
    const int Y = yi & 255;
    const uint8_t aa = m_perm[m_perm[X] + Y];
    const uint8_t ab = m_perm[m_perm[X] + Y + 1];
    const uint8_t ba = m_perm[m_perm[X + 1] + Y];
    const uint8_t bb = m_perm[m_perm[X + 1] + Y + 1];
    const float u = fade(x);
    const float v = fade(y);
    const float r = lerp(lerp(grad2(aa, x, y), grad2(ba, x - 1, y), u),
                         lerp(grad2(ab, x, y - 1), grad2(bb, x - 1, y - 1), u), v);
    return r * 0.7071f; // max |grad . offset| is ~1.41 for the diagonal gradients
}

float GradientNoise::sample(glm::vec3 p) const {
    const int xi = floorInt(p.x);
    const int yi = floorInt(p.y);
    const int zi = floorInt(p.z);
    const float x = p.x - static_cast<float>(xi);
    const float y = p.y - static_cast<float>(yi);
    const float z = p.z - static_cast<float>(zi);
    const int X = xi & 255;
    const int Y = yi & 255;
    const int Z = zi & 255;
    const int A = m_perm[X] + Y;
    const int AA = m_perm[A] + Z;
    const int AB = m_perm[A + 1] + Z;
    const int B = m_perm[X + 1] + Y;
    const int BA = m_perm[B] + Z;
    const int BB = m_perm[B + 1] + Z;
    const float u = fade(x);
    const float v = fade(y);
    const float w = fade(z);
    return lerp(lerp(lerp(grad3(m_perm[AA], x, y, z), grad3(m_perm[BA], x - 1, y, z), u),
                     lerp(grad3(m_perm[AB], x, y - 1, z), grad3(m_perm[BB], x - 1, y - 1, z), u), v),
                lerp(lerp(grad3(m_perm[AA + 1], x, y, z - 1), grad3(m_perm[BA + 1], x - 1, y, z - 1), u),
                     lerp(grad3(m_perm[AB + 1], x, y - 1, z - 1), grad3(m_perm[BB + 1], x - 1, y - 1, z - 1), u), v),
                w);
}

float GradientNoise::fbm(glm::vec2 p, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        // Offset each octave so lattice points of different octaves do not line up.
        sum += amplitude * sample(p + glm::vec2(static_cast<float>(i) * 17.31f, static_cast<float>(i) * -9.77f));
        norm += amplitude;
        amplitude *= gain;
        p *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float GradientNoise::fbm(glm::vec3 p, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const float o = static_cast<float>(i);
        sum += amplitude * sample(p + glm::vec3(o * 17.31f, o * -9.77f, o * 5.13f));
        norm += amplitude;
        amplitude *= gain;
        p *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float GradientNoise::ridged(glm::vec2 p, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        float r = 1.0f - std::abs(sample(p + glm::vec2(static_cast<float>(i) * 31.7f, 0.0f)));
        r *= r;
        sum += amplitude * r;
        norm += amplitude;
        amplitude *= gain;
        p *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

} // namespace zodar
