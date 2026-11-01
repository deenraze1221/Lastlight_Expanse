#pragma once

#include "Scene/Voxel/VoxelChunk.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Block registry and shape library (Phase V1, docs/guide/VOXELS.md §3). A voxel id is a block
// state: material + shape + yaw rotation. Ids without block states (legacy palettes) are full
// cubes whose material index equals the id, which keeps the cube mesher output unchanged.
namespace zodar::voxel {

// Face directions; also bit positions in face masks (MeshOptions::sealFaces, BlockInfo::fullFaces).
enum class Face : uint8_t { NegX, PosX, NegY, PosY, NegZ, PosZ };

constexpr uint8_t faceBit(int face) { return static_cast<uint8_t>(1u << face); }
constexpr uint8_t kAllFaces = 0x3F;

enum class FaceCoverage : uint8_t { None, Partial, Full };

// Convex or star-shaped planar polygon in cell-local space ([0, 1]^3). Corners are counter-
// clockwise seen from outside the solid; triangulated as a fan around corners[0], so every
// corner must be visible from corners[0].
struct ShapePolygon {
    std::vector<glm::vec3> corners;
};

// Cell-local collision box (min < max on every axis).
struct ShapeBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{1.0f};
};

// Authoring form of a shape: closed, outward-oriented surface of the solid part of one cell.
struct ShapeTemplate {
    std::string name;
    std::vector<ShapePolygon> polygons;
    // Exact for boxy shapes, a stepped approximation for slopes and curves.
    std::vector<ShapeBox> collision;
    // Darkens neighbouring faces in the voxel ambient-occlusion term like a full cube does.
    bool aoOccluder = false;
};

// A validated shape with derived data. Produced by ShapeLibrary::add and rotateShape.
struct Shape {
    struct Polygon {
        std::vector<glm::vec3> corners;
        glm::vec3 normal{0.0f};
        int8_t boundaryFace = -1; // Face index if the polygon lies on that cell face, else -1 (interior)
    };
    std::string name;
    std::vector<Polygon> polygons;
    std::vector<ShapeBox> collision;
    // Per face (Face order): how much of the cell face the solid covers.
    std::array<FaceCoverage, 6> coverage{};
    // Per face: 4 x 4 coverage samples, bit (j * 4 + i) at ((i + 0.5) / 4, (j + 0.5) / 4) along
    // the face's (u, v) axes, u = (axis + 1) % 3, v = (axis + 2) % 3 (the mesher's face axes).
    std::array<uint16_t, 6> coverageMask{};
    float volume = 0.0f;   // fraction of the cell, 0..1
    uint8_t fillEighths = 0; // round(volume * 8): weight in the LOD majority rule
    bool aoOccluder = false;

    uint8_t fullFaces() const;
    // Point-in-solid test in cell-local space. Deterministic and libm-free: majority vote of
    // three crossing-parity rays in fixed generic directions against the fan triangles, so a
    // ray that grazes an edge or vertex can only spoil its own vote. Points outside [0, 1]^3
    // are outside; points within about 1e-6 of the surface may be classified either way.
    bool contains(const glm::vec3& p) const;
};

// Yaw rotation in 90-degree steps about the cell's vertical centre line (right-handed about +Y:
// one step turns +X into -Z). Recomputes all derived data.
Shape rotateShape(const Shape& shape, int yawSteps);

// Registered shapes, index = shape id. Index 0 is always "cube" (the greedy mesher path).
// Extensible by registration; games add their own shapes after the built-ins.
class ShapeLibrary {
public:
    static constexpr uint16_t kCube = 0;

    ShapeLibrary(); // contains only "cube"
    // Cube plus the engine's built-in shapes: slab_low, slab_high, stair, wedge, corner_inner,
    // corner_outer, quarter_round, quarter_round_inner, panel_thin (VOXELS.md §3).
    static ShapeLibrary builtins();

    // Validates and adds a shape; returns its index. Fails (zodar::fail) on invalid geometry
    // or a duplicate name.
    uint16_t add(const ShapeTemplate& shape);
    std::optional<uint16_t> find(std::string_view name) const;
    const Shape& operator[](uint16_t index) const { return m_shapes.at(index); }
    size_t size() const { return m_shapes.size(); }

private:
    std::vector<Shape> m_shapes;
};

// Block state of one voxel id.
struct BlockState {
    static constexpr uint8_t kNoCollision = 1u << 0;
    static constexpr uint8_t kTransparent = 1u << 1; // reserved: not supported by the mesher yet

    uint16_t material = 0; // index into VoxelPalette::colors / faceLayers
    uint16_t shape = ShapeLibrary::kCube;
    uint8_t rotation = 0;  // yaw steps 0..3 (flip reserved)
    uint8_t flags = 0;
};

// Voxel id -> block state and the material table. Linear RGBA colour per material, written into
// vertex colours, which the engine's surface shaders multiply with the material base colour.
//
// Optional texture layers: when `faceLayers` is non-empty the mesher writes the layer of each
// face into the vertex colour's alpha (exact small integer; voxels are opaque) for shaders that
// sample a texture array (e.g. zodarSampleArray). Order per material: top (+Y), side, bottom (-Y).
//
// Legacy mode (`blocks` empty): every non-empty id is a cube with material index = id.
// Registry mode: `blocks[id]` for ids below blocks.size() (blocks[0] is ignored, id 0 is empty);
// higher ids (unknown blocks, e.g. from corrupted or newer data) are cubes with the explicit
// fallback material `kFallbackMaterial` (fallback colour, texture layer 0), never a real
// material (V2; V1 used material = id there).
struct VoxelPalette {
    // Out of range of `colors` by construction (registry palettes hold fewer materials).
    static constexpr uint16_t kFallbackMaterial = 0xFFFF;

    std::vector<glm::vec4> colors;
    std::vector<std::array<uint16_t, 3>> faceLayers{};
    std::vector<BlockState> blocks{};
    std::shared_ptr<const ShapeLibrary> shapes{}; // required when a block uses a non-cube shape

    glm::vec4 color(uint32_t material) const {
        return material < colors.size() ? colors[material] : glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    bool hasLayers() const { return !faceLayers.empty(); }
    // face: 0 top, 1 side, 2 bottom.
    float layer(uint32_t material, int face) const {
        return material < faceLayers.size() ? float(faceLayers[material][face]) : 0.0f;
    }
};

// Per-id data the mesher and the world need, resolved once from a palette.
struct BlockInfo {
    // Bits of BlockTable::cellFlags.
    static constexpr uint8_t kAoOccluder = 1u << 6;
    static constexpr uint8_t kCube = 1u << 7;

    uint16_t material = 0;
    uint8_t flags = 0;       // BlockState flags
    uint8_t fill = 0;        // eighths of the cell
    uint8_t fullFaces = 0;   // faceBit(face): the face is fully covered (after rotation)
    bool solid = false;      // not empty
    bool cube = false;
    bool aoOccluder = false;
    const Shape* shape = nullptr; // rotated geometry of non-cube shapes
};

// Immutable lookup table built from a VoxelPalette (validates it). Thread-safe to read.
class BlockTable {
public:
    explicit BlockTable(const VoxelPalette& palette);
    BlockTable(const BlockTable&) = delete;
    BlockTable& operator=(const BlockTable&) = delete;

    BlockInfo info(Voxel v) const;
    // fullFaces bits | kAoOccluder | kCube; 0 for empty.
    uint8_t cellFlags(Voxel v) const { return v < m_flags.size() ? m_flags[v] : kLegacyCubeFlags; }
    // Legacy: material = id. Registry: ids beyond `blocks` map to VoxelPalette::kFallbackMaterial.
    uint16_t material(Voxel v) const {
        return v < m_material.size() ? m_material[v] : m_legacy ? v : VoxelPalette::kFallbackMaterial;
    }
    uint8_t fill(Voxel v) const { return v < m_fill.size() ? m_fill[v] : uint8_t(8); }
    bool isCube(Voxel v) const { return (cellFlags(v) & BlockInfo::kCube) != 0; }
    // Rotated geometry of a non-cube id, else nullptr.
    const Shape* shape(Voxel v) const;
    // Smallest cube id (>= 1); an out-of-range id when the registry has no cube block.
    Voxel firstCube() const { return m_firstCube; }
    bool legacy() const { return m_legacy; }
    // No id uses a non-cube shape (cube-only fast paths).
    bool allCubes() const { return m_rotated.empty(); }

    const VoxelPalette& palette() const { return m_palette; }

private:
    static constexpr uint8_t kLegacyCubeFlags = kAllFaces | BlockInfo::kAoOccluder | BlockInfo::kCube;

    VoxelPalette m_palette;
    bool m_legacy = true;
    Voxel m_firstCube = 1;
    std::vector<uint8_t> m_flags;
    std::vector<uint16_t> m_material;
    std::vector<uint8_t> m_fill;
    std::vector<uint8_t> m_stateFlags;
    std::vector<int32_t> m_shapeIndex;             // index into m_rotated, -1 for cubes/empty
    std::vector<std::unique_ptr<Shape>> m_rotated; // unique (shape, rotation) pairs in use
};

} // namespace zodar::voxel
