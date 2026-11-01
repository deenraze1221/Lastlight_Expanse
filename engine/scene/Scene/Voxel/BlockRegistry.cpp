#include "Scene/Voxel/BlockRegistry.h"

#include "Core/Error.h"

#include <cmath>
#include <map>
#include <string>

namespace zodar::voxel {
namespace {

constexpr float kEps = 1e-5f;

[[noreturn]] void shapeError(const std::string& shape, const std::string& message) {
    fail("Voxel shape '" + shape + "': " + message);
}

// Twice the signed area vector of the fan triangulation around corners[0].
glm::vec3 fanCross(const std::vector<glm::vec3>& c) {
    glm::vec3 n(0.0f);
    for (size_t i = 1; i + 1 < c.size(); ++i) {
        n += glm::cross(c[i] - c[0], c[i + 1] - c[0]);
    }
    return n;
}

// Signed volume of the closed surface relative to `ref` (divergence theorem over fan triangles).
double signedVolume(const std::vector<Shape::Polygon>& polygons, const glm::dvec3& ref) {
    double v = 0.0;
    for (const auto& p : polygons) {
        const glm::dvec3 a = glm::dvec3(p.corners[0]) - ref;
        for (size_t i = 1; i + 1 < p.corners.size(); ++i) {
            const glm::dvec3 b = glm::dvec3(p.corners[i]) - ref;
            const glm::dvec3 c = glm::dvec3(p.corners[i + 1]) - ref;
            v += glm::dot(a, glm::cross(b, c));
        }
    }
    return v / 6.0;
}

bool inTriangle2(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    const auto side = [](const glm::vec2& o, const glm::vec2& d, const glm::vec2& q) {
        return (d.x - o.x) * (q.y - o.y) - (d.y - o.y) * (q.x - o.x);
    };
    const float s1 = side(a, b, p);
    const float s2 = side(b, c, p);
    const float s3 = side(c, a, p);
    return (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
}

// Validates the geometry and computes all derived data.
Shape analyse(std::string name, const std::vector<std::vector<glm::vec3>>& polygons, std::vector<ShapeBox> collision,
              bool aoOccluder) {
    if (name.empty()) {
        fail("Voxel shape: empty name");
    }
    if (polygons.empty()) {
        shapeError(name, "no polygons");
    }
    Shape s;
    s.name = std::move(name);
    s.aoOccluder = aoOccluder;
    std::array<double, 6> area{};
    for (size_t pi = 0; pi < polygons.size(); ++pi) {
        const auto& corners = polygons[pi];
        const std::string where = "polygon " + std::to_string(pi);
        if (corners.size() < 3) {
            shapeError(s.name, where + " has fewer than 3 corners");
        }
        for (const glm::vec3& c : corners) {
            for (int a = 0; a < 3; ++a) {
                if (!(c[a] >= -kEps && c[a] <= 1.0f + kEps)) {
                    shapeError(s.name, where + " leaves the cell");
                }
            }
        }
        const glm::vec3 cross = fanCross(corners);
        const float len = glm::length(cross);
        if (len < kEps) {
            shapeError(s.name, where + " is degenerate");
        }
        Shape::Polygon p;
        p.corners = corners;
        p.normal = cross / len;
        for (const glm::vec3& c : corners) {
            if (std::abs(glm::dot(c - corners[0], p.normal)) > 1e-4f) {
                shapeError(s.name, where + " is not planar");
            }
        }
        for (int a = 0; a < 3; ++a) {
            if (std::abs(std::abs(p.normal[a]) - 1.0f) > kEps) {
                continue;
            }
            const float plane = corners[0][a];
            if (std::abs(plane) > kEps && std::abs(plane - 1.0f) > kEps) {
                break; // axis-aligned but inside the cell
            }
            const bool onMax = std::abs(plane - 1.0f) <= kEps;
            if ((p.normal[a] > 0.0f) != onMax) {
                shapeError(s.name, where + " lies on a cell face but points into the cell");
            }
            p.boundaryFace = static_cast<int8_t>(a * 2 + (onMax ? 1 : 0));
            area[static_cast<size_t>(p.boundaryFace)] += 0.5 * static_cast<double>(len);
        }
        s.polygons.push_back(std::move(p));
    }
    // Coverage per face, plus a 4 x 4 sample mask for partial faces.
    for (int f = 0; f < 6; ++f) {
        const double a = area[static_cast<size_t>(f)];
        if (a > 1.0 + 1e-4) {
            shapeError(s.name, "overlapping polygons on face " + std::to_string(f));
        }
        s.coverage[static_cast<size_t>(f)] = std::abs(a - 1.0) <= 1e-4 ? FaceCoverage::Full
                                             : a > 1e-6               ? FaceCoverage::Partial
                                                                      : FaceCoverage::None;
        uint16_t mask = 0;
        if (s.coverage[static_cast<size_t>(f)] == FaceCoverage::Full) {
            mask = 0xFFFF;
        } else if (s.coverage[static_cast<size_t>(f)] == FaceCoverage::Partial) {
            const int axis = f >> 1;
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;
            for (const auto& p : s.polygons) {
                if (p.boundaryFace != f) continue;
                for (int j = 0; j < 4; ++j) {
                    for (int i = 0; i < 4; ++i) {
                        const glm::vec2 q((static_cast<float>(i) + 0.5f) / 4.0f, (static_cast<float>(j) + 0.5f) / 4.0f);
                        const glm::vec2 c0(p.corners[0][u], p.corners[0][v]);
                        for (size_t k = 1; k + 1 < p.corners.size(); ++k) {
                            if (inTriangle2(q, c0, {p.corners[k][u], p.corners[k][v]},
                                            {p.corners[k + 1][u], p.corners[k + 1][v]})) {
                                mask = static_cast<uint16_t>(mask | (1u << (j * 4 + i)));
                                break;
                            }
                        }
                    }
                }
            }
        }
        s.coverageMask[static_cast<size_t>(f)] = mask;
    }
    // Closed and consistently outward: the volume must not depend on the reference point.
    const double v0 = signedVolume(s.polygons, {0.0, 0.0, 0.0});
    const double v1 = signedVolume(s.polygons, {0.37, 0.61, 0.23});
    if (std::abs(v0 - v1) > 1e-4 || v0 <= 1e-6 || v0 > 1.0 + 1e-4) {
        shapeError(s.name, "surface is not closed and outward-facing (volume " + std::to_string(v0) + " / " +
                               std::to_string(v1) + ")");
    }
    s.volume = static_cast<float>(v0);
    s.fillEighths = static_cast<uint8_t>(std::lround(v0 * 8.0));
    for (const ShapeBox& b : collision) {
        for (int a = 0; a < 3; ++a) {
            if (!(b.min[a] >= -kEps && b.max[a] <= 1.0f + kEps && b.min[a] < b.max[a])) {
                shapeError(s.name, "collision box outside the cell or empty");
            }
        }
    }
    s.collision = std::move(collision);
    return s;
}

// One yaw step: (x, z) -> (z, 1 - x).
glm::vec3 yaw(const glm::vec3& p) { return {p.z, p.y, 1.0f - p.x}; }

} // namespace

uint8_t Shape::fullFaces() const {
    uint8_t bits = 0;
    for (int f = 0; f < 6; ++f) {
        if (coverage[static_cast<size_t>(f)] == FaceCoverage::Full) bits |= faceBit(f);
    }
    return bits;
}

Shape rotateShape(const Shape& shape, int yawSteps) {
    const int steps = ((yawSteps % 4) + 4) % 4;
    if (steps == 0) {
        return shape;
    }
    std::vector<std::vector<glm::vec3>> polygons;
    for (const auto& p : shape.polygons) {
        auto corners = p.corners;
        for (glm::vec3& c : corners) {
            for (int i = 0; i < steps; ++i) c = yaw(c);
        }
        polygons.push_back(std::move(corners));
    }
    std::vector<ShapeBox> boxes;
    for (const ShapeBox& b : shape.collision) {
        glm::vec3 lo = b.min;
        glm::vec3 hi = b.max;
        for (int i = 0; i < steps; ++i) {
            lo = yaw(lo);
            hi = yaw(hi);
        }
        boxes.push_back({glm::min(lo, hi), glm::max(lo, hi)});
    }
    return analyse(shape.name, polygons, std::move(boxes), shape.aoOccluder);
}

ShapeLibrary::ShapeLibrary() {
    ShapeTemplate cube;
    cube.name = "cube";
    cube.aoOccluder = true;
    cube.collision = {{glm::vec3(0.0f), glm::vec3(1.0f)}};
    // Same corner order as the mesher's quads: (u0,v0), (u1,v0), (u1,v1), (u0,v1), reversed on
    // negative faces.
    for (int f = 0; f < 6; ++f) {
        const int a = f >> 1;
        const int u = (a + 1) % 3;
        const int v = (a + 2) % 3;
        const float plane = (f & 1) ? 1.0f : 0.0f;
        const float cu[4] = {0, 1, 1, 0};
        const float cv[4] = {0, 0, 1, 1};
        ShapePolygon poly;
        for (int c = 0; c < 4; ++c) {
            glm::vec3 p;
            p[a] = plane;
            p[u] = cu[c];
            p[v] = cv[c];
            poly.corners.push_back(p);
        }
        if (!(f & 1)) std::swap(poly.corners[1], poly.corners[3]);
        cube.polygons.push_back(std::move(poly));
    }
    add(cube);
}

uint16_t ShapeLibrary::add(const ShapeTemplate& shape) {
    if (find(shape.name)) {
        fail("Voxel shape '" + shape.name + "' is already registered");
    }
    if (m_shapes.size() >= 0xFFFF) {
        fail("Voxel shape library is full");
    }
    std::vector<std::vector<glm::vec3>> polygons;
    for (const auto& p : shape.polygons) polygons.push_back(p.corners);
    m_shapes.push_back(analyse(shape.name, polygons, shape.collision, shape.aoOccluder));
    return static_cast<uint16_t>(m_shapes.size() - 1);
}

std::optional<uint16_t> ShapeLibrary::find(std::string_view name) const {
    for (size_t i = 0; i < m_shapes.size(); ++i) {
        if (m_shapes[i].name == name) return static_cast<uint16_t>(i);
    }
    return std::nullopt;
}

BlockTable::BlockTable(const VoxelPalette& palette) : m_palette(palette) {
    m_legacy = palette.blocks.empty();
    if (m_legacy) {
        // Cover the palette's ids so lookups stay in the table; ids beyond are cubes as well.
        const size_t n = std::max<size_t>(palette.colors.size(), 1);
        m_flags.assign(n, kLegacyCubeFlags);
        m_flags[0] = 0;
        m_material.resize(n);
        for (size_t i = 0; i < n; ++i) m_material[i] = static_cast<uint16_t>(i);
        m_fill.assign(n, 8);
        m_fill[0] = 0;
        m_stateFlags.assign(n, 0);
        m_shapeIndex.assign(n, -1);
        m_firstCube = 1;
        return;
    }
    const size_t n = palette.blocks.size();
    m_flags.assign(n, 0);
    m_material.assign(n, 0);
    m_fill.assign(n, 0);
    m_stateFlags.assign(n, 0);
    m_shapeIndex.assign(n, -1);
    m_firstCube = static_cast<Voxel>(std::min<size_t>(n, 0xFFFF));
    std::map<std::pair<uint16_t, uint8_t>, int32_t> rotatedIndex;
    for (size_t id = 1; id < n; ++id) {
        const BlockState& b = palette.blocks[id];
        const std::string where = "Block state " + std::to_string(id) + ": ";
        ZODAR_ENSURE(b.material < palette.colors.size(), where + "material " + std::to_string(b.material) + " out of range");
        ZODAR_ENSURE(b.rotation < 4, where + "rotation " + std::to_string(b.rotation) + " out of range (0..3)");
        m_material[id] = b.material;
        m_stateFlags[id] = b.flags;
        if (b.shape == ShapeLibrary::kCube) {
            m_flags[id] = kLegacyCubeFlags;
            m_fill[id] = 8;
            m_firstCube = std::min<Voxel>(m_firstCube, static_cast<Voxel>(id));
            continue;
        }
        ZODAR_ENSURE(palette.shapes != nullptr, where + "uses a shape but the palette has no shape library");
        ZODAR_ENSURE(b.shape < palette.shapes->size(), where + "shape " + std::to_string(b.shape) + " out of range");
        const auto key = std::make_pair(b.shape, b.rotation);
        auto it = rotatedIndex.find(key);
        if (it == rotatedIndex.end()) {
            m_rotated.push_back(std::make_unique<Shape>(rotateShape((*palette.shapes)[b.shape], b.rotation)));
            it = rotatedIndex.emplace(key, static_cast<int32_t>(m_rotated.size() - 1)).first;
        }
        const Shape& s = *m_rotated[static_cast<size_t>(it->second)];
        m_shapeIndex[id] = it->second;
        m_flags[id] = static_cast<uint8_t>(s.fullFaces() | (s.aoOccluder ? BlockInfo::kAoOccluder : 0));
        m_fill[id] = s.fillEighths;
    }
}

const Shape* BlockTable::shape(Voxel v) const {
    if (v >= m_shapeIndex.size() || m_shapeIndex[v] < 0) return nullptr;
    return m_rotated[static_cast<size_t>(m_shapeIndex[v])].get();
}

BlockInfo BlockTable::info(Voxel v) const {
    BlockInfo i;
    if (v == kEmpty) return i;
    const uint8_t f = cellFlags(v);
    i.material = material(v);
    i.flags = v < m_stateFlags.size() ? m_stateFlags[v] : uint8_t(0);
    i.fill = fill(v);
    i.fullFaces = static_cast<uint8_t>(f & kAllFaces);
    i.solid = true;
    i.cube = (f & BlockInfo::kCube) != 0;
    i.aoOccluder = (f & BlockInfo::kAoOccluder) != 0;
    i.shape = shape(v);
    return i;
}

} // namespace zodar::voxel
