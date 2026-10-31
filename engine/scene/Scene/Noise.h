#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace zodar {

// Seeded gradient (Perlin-style) noise. Deterministic across platforms and compilers for a
// given seed: the permutation table is built with a fixed integer PRNG and evaluation uses
// only float arithmetic without transcendental functions.
class GradientNoise {
public:
    explicit GradientNoise(uint64_t seed = 0);

    // Range approximately [-1, 1].
    float sample(glm::vec2 p) const;
    float sample(glm::vec3 p) const;

    // Fractal Brownian motion: `octaves` layers, each `lacunarity` times the frequency and
    // `gain` times the amplitude of the previous one. Normalised to approximately [-1, 1].
    float fbm(glm::vec2 p, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
    float fbm(glm::vec3 p, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
    // 1 - |noise|, squared and accumulated: sharp ridges (mountain ranges). Range [0, 1].
    float ridged(glm::vec2 p, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

    uint64_t seed() const { return m_seed; }

private:
    uint64_t m_seed;
    std::array<uint8_t, 512> m_perm{};
};

// Stateless 64-bit hash (SplitMix64 finaliser) for deterministic per-coordinate randomness.
uint64_t hash64(uint64_t x);
uint64_t hashCoords(int32_t x, int32_t y, int32_t z, uint64_t seed);

} // namespace zodar
