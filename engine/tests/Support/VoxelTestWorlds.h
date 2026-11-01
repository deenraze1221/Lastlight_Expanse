#pragma once

// Deterministic voxel test worlds shared by the V3 query and LOD tests (raycasts, solid boxes,
// conservative LOD, seams). Inputs come from integer hashing only, so results do not depend on
// libm; golden values recorded from them are part of the tests.

#include "Scene/Voxel/BlockRegistry.h"
#include "Scene/Voxel/VoxelWorld.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <memory>
#include <vector>

namespace zodar::test {

inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline uint64_t hash3(int x, int y, int z, uint64_t seed) {
    return splitmix64(seed ^ splitmix64(static_cast<uint32_t>(x) ^
                                        splitmix64(static_cast<uint32_t>(y) ^ splitmix64(static_cast<uint32_t>(z)))));
}

// Registry palette with every built-in shape at every rotation.
//   id 1            cube, material 1
//   id 2            cube, material 2, kNoCollision
//   id 3 + 4s + r   shape s (library index, 0 = cube) at rotation r, material 3
//   last + 1..      same shape ids again with kNoCollision (noCollisionOffset)
struct ShapeRegistry {
    std::shared_ptr<voxel::ShapeLibrary> lib = std::make_shared<voxel::ShapeLibrary>(voxel::ShapeLibrary::builtins());
    voxel::VoxelPalette palette;
    voxel::Voxel cube = 1;
    voxel::Voxel ghostCube = 2;
    voxel::Voxel noCollisionOffset = 0;

    ShapeRegistry() {
        using voxel::BlockState;
        palette.colors = {glm::vec4(0), glm::vec4(0.5f, 0.5f, 0.5f, 1), glm::vec4(0.2f, 0.7f, 0.2f, 1),
                          glm::vec4(0.8f, 0.6f, 0.4f, 1)};
        palette.shapes = lib;
        const auto shapes = static_cast<uint16_t>(lib->size());
        noCollisionOffset = static_cast<voxel::Voxel>(shapes * 4);
        palette.blocks.resize(3 + 2 * shapes * 4);
        palette.blocks[1] = {1, voxel::ShapeLibrary::kCube, 0, 0};
        palette.blocks[2] = {2, voxel::ShapeLibrary::kCube, 0, BlockState::kNoCollision};
        for (uint16_t s = 0; s < shapes; ++s) {
            for (uint8_t r = 0; r < 4; ++r) {
                palette.blocks[id(s, r)] = {3, s, r, 0};
                palette.blocks[id(s, r) + noCollisionOffset] = {3, s, r, BlockState::kNoCollision};
            }
        }
    }
    static voxel::Voxel id(uint16_t shape, int rotation) { return static_cast<voxel::Voxel>(3 + shape * 4 + rotation); }
    voxel::Voxel id(const char* name, int rotation = 0) const { return id(*lib->find(name), rotation); }
    voxel::Voxel shapeCount() const { return static_cast<voxel::Voxel>(lib->size()); }
};

// Legacy palette: 4 cube materials (ids 1..4), no registry.
inline voxel::VoxelPalette legacyTestPalette() {
    voxel::VoxelPalette p;
    p.colors = {glm::vec4(0), glm::vec4(0.3f, 0.5f, 0.2f, 1), glm::vec4(0.5f, 0.4f, 0.3f, 1),
                glm::vec4(0.6f, 0.6f, 0.6f, 1), glm::vec4(0.9f, 0.9f, 0.8f, 1)};
    return p;
}

// Hashed terrain: ground height per column on a coarse lattice, solid below. `Mode` picks the
// ids: Legacy (ids 1..4), RegistryCubes (cube ids 1 and 2, incl. kNoCollision cubes) or Shapes
// (cubes below, shapes of every kind, rotation and collision flag on and above the surface).
class QueryTerrain final : public voxel::IVoxelGenerator {
public:
    enum class Mode { Legacy, RegistryCubes, Shapes };
    QueryTerrain(uint64_t seed, Mode mode, voxel::Voxel shapeCount = 0, voxel::Voxel noCollisionOffset = 0)
        : m_seed(seed), m_mode(mode), m_shapes(shapeCount), m_noCollision(noCollisionOffset) {}

    int height(int x, int z) const {
        const int cx = x >> 3;
        const int cz = z >> 3;
        int sum = 0;
        for (int dz = 0; dz <= 1; ++dz) {
            for (int dx = 0; dx <= 1; ++dx) sum += static_cast<int>(hash3(cx + dx, 0, cz + dz, m_seed) % 12);
        }
        return sum / 4 + static_cast<int>(hash3(x, 1, z, m_seed) % 3) - 2;
    }

    voxel::Voxel at(int x, int y, int z) const {
        const int h = height(x, z);
        const uint64_t r = hash3(x, y, z, m_seed + 1);
        if (y < h) {
            if (r % 23 == 0) return voxel::kEmpty; // small holes
            switch (m_mode) {
            case Mode::Legacy: return static_cast<voxel::Voxel>(1 + r % 4);
            case Mode::RegistryCubes: return r % 7 == 0 ? voxel::Voxel(2) : voxel::Voxel(1);
            case Mode::Shapes: return r % 11 == 0 ? shapeId(r >> 8) : voxel::Voxel(1);
            }
        }
        // Scattered blocks on and above the surface.
        if (y <= h + 3 && r % 5 == 0) {
            switch (m_mode) {
            case Mode::Legacy: return static_cast<voxel::Voxel>(1 + (r >> 8) % 4);
            case Mode::RegistryCubes: return (r >> 8) % 3 == 0 ? voxel::Voxel(2) : voxel::Voxel(1);
            case Mode::Shapes: return shapeId(r >> 8);
            }
        }
        return voxel::kEmpty;
    }

    void generate(const voxel::ChunkCoord& coord, voxel::VoxelChunk& out) const override {
        const glm::ivec3 o = coord.originVoxel();
        for (int y = 0; y < voxel::kChunkSize; ++y) {
            for (int z = 0; z < voxel::kChunkSize; ++z) {
                for (int x = 0; x < voxel::kChunkSize; ++x) {
                    const voxel::Voxel v = at(o.x + x, o.y + y, o.z + z);
                    if (v != voxel::kEmpty) out.set(x, y, z, v);
                }
            }
        }
        out.compact();
    }

private:
    // Any shape (incl. cube) at any rotation; one in 8 without collision.
    voxel::Voxel shapeId(uint64_t r) const {
        const auto s = static_cast<uint16_t>(r % m_shapes);
        const int rot = static_cast<int>((r >> 8) % 4);
        const voxel::Voxel base = ShapeRegistry::id(s, rot);
        return (r >> 12) % 8 == 0 ? static_cast<voxel::Voxel>(base + m_noCollision) : base;
    }

    uint64_t m_seed;
    Mode m_mode;
    voxel::Voxel m_shapes;
    voxel::Voxel m_noCollision;
};

inline voxel::StreamingSettings queryWorldSettings(float voxelSize) {
    voxel::StreamingSettings s;
    s.voxelSize = voxelSize;
    s.viewRadius = 2;
    s.minChunkY = -1;
    s.maxChunkY = 1;
    s.lodDistances = {1.0f, 2.0f};
    s.maxJobsPerUpdate = 1000;
    s.maxJobsInFlight = 1000;
    return s;
}

inline std::string hex64(uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(v));
    return buf;
}

inline void settleWorld(voxel::VoxelWorld& world, const glm::vec3& observer) {
    for (int i = 0; i < 64 && (i == 0 || !world.isSettled()); ++i) {
        world.update(observer);
        world.finishPendingWork();
        (void)world.takeMeshEvents();
    }
    REQUIRE(world.isSettled());
}

} // namespace zodar::test
