#include "Scene/Voxel/ChunkMesher.h"

#include "Core/Error.h"

#include <string>

namespace zodar::voxel {
namespace {

// Padded cell grid for one LOD: (n + 2)^3 cells, the outer layer taken from neighbours. Each
// cell keeps its voxel id and BlockTable::cellFlags (full faces, AO occluder, cube). At LOD > 0
// every non-empty cell is meshed as a cube (shapes render as cubes there until V3).
class CellGrid {
public:
    CellGrid(const ChunkNeighborhood& nb, const BlockTable& table, int lod, uint8_t sealFaces)
        : m_n(kChunkSize >> lod), m_p(m_n + 2), m_cells(static_cast<size_t>(m_p) * m_p * m_p, kEmpty),
          m_flags(m_cells.size(), uint8_t(0)) {
        const int s = 1 << lod;
        const VoxelChunk* center = nb.center();
        const bool uniformCenter = center && center->isUniform();
        for (int y = -1; y <= m_n; ++y) {
            for (int z = -1; z <= m_n; ++z) {
                for (int x = -1; x <= m_n; ++x) {
                    const bool inside = x >= 0 && y >= 0 && z >= 0 && x < m_n && y < m_n && z < m_n;
                    Voxel v;
                    if (inside && uniformCenter) {
                        v = center->uniformValue();
                    } else if (s == 1) {
                        v = nb.get(x, y, z);
                    } else if (table.allCubes()) {
                        v = downsample<false>(nb, table, x * s, y * s, z * s, s);
                    } else {
                        v = downsample<true>(nb, table, x * s, y * s, z * s, s);
                    }
                    const size_t i = index(x, y, z);
                    m_cells[i] = v;
                    if (v != kEmpty) {
                        const uint8_t f = s == 1 ? table.cellFlags(v) : kCubeFlags;
                        m_flags[i] = f;
                        m_hasShapes = m_hasShapes || (inside && !(f & BlockInfo::kCube));
                    }
                }
            }
        }
        // Sealing: the neighbour slab on a sealed side reads as empty.
        for (int f = 0; f < 6; ++f) {
            if (!(sealFaces & (1u << f))) {
                continue;
            }
            const int axis = f >> 1;
            const int layer = (f & 1) ? m_n : -1;
            for (int a = -1; a <= m_n; ++a) {
                for (int b = -1; b <= m_n; ++b) {
                    glm::ivec3 c;
                    c[axis] = layer;
                    c[(axis + 1) % 3] = a;
                    c[(axis + 2) % 3] = b;
                    m_cells[index(c.x, c.y, c.z)] = kEmpty;
                    m_flags[index(c.x, c.y, c.z)] = 0;
                }
            }
        }
    }

    static constexpr uint8_t kCubeFlags = kAllFaces | BlockInfo::kAoOccluder | BlockInfo::kCube;

    int size() const { return m_n; }
    bool hasShapes() const { return m_hasShapes; }
    Voxel at(const glm::ivec3& c) const { return m_cells[index(c.x, c.y, c.z)]; }
    uint8_t flags(const glm::ivec3& c) const { return m_flags[index(c.x, c.y, c.z)]; }
    // The cell's face `face` (Face order) is fully covered.
    bool covers(const glm::ivec3& c, int face) const { return (flags(c) & faceBit(face)) != 0; }
    bool occludes(const glm::ivec3& c) const { return (flags(c) & BlockInfo::kAoOccluder) != 0; }

private:
    size_t index(int x, int y, int z) const {
        return (static_cast<size_t>(y + 1) * m_p + static_cast<size_t>(z + 1)) * m_p + static_cast<size_t>(x + 1);
    }

    // Majority rule weighted by fill (cubes 8/8, shapes their fill eighths); the cell takes
    // the topmost non-empty voxel.
    template <bool kWeighted>
    static Voxel downsample(const ChunkNeighborhood& nb, const BlockTable& table, int x0, int y0, int z0, int s) {
        int filled = 0;
        Voxel top = kEmpty;
        for (int y = y0 + s - 1; y >= y0; --y) {
            for (int z = z0; z < z0 + s; ++z) {
                for (int x = x0; x < x0 + s; ++x) {
                    const Voxel v = nb.get(x, y, z);
                    if (v != kEmpty) {
                        if constexpr (kWeighted) {
                            filled += table.fill(v);
                        } else {
                            filled += 8;
                        }
                        if (top == kEmpty) {
                            top = v;
                        }
                    }
                }
            }
        }
        return filled * 2 >= s * s * s * 8 ? top : kEmpty;
    }

    int m_n;
    int m_p;
    std::vector<Voxel> m_cells;
    std::vector<uint8_t> m_flags;
    bool m_hasShapes = false;
};

bool uniformAo(uint32_t aoBits) {
    return aoBits == 0x00u || aoBits == 0x55u || aoBits == 0xAAu || aoBits == 0xFFu;
}

} // namespace

MeshData meshChunk(const ChunkNeighborhood& neighborhood, const VoxelPalette& palette, const MeshOptions& options,
                   MeshStats* stats) {
    const BlockTable table(palette);
    return meshChunk(neighborhood, table, options, stats);
}

MeshData meshChunk(const ChunkNeighborhood& neighborhood, const BlockTable& table, const MeshOptions& options,
                   MeshStats* stats) {
    ZODAR_ENSURE(neighborhood.center() != nullptr, "meshChunk needs the centre chunk");
    ZODAR_ENSURE(options.lod >= 0 && options.lod <= kMaxLod, "LOD out of range: " + std::to_string(options.lod));

    MeshData mesh;
    mesh.name = "VoxelChunk";
    MeshStats localStats;
    const VoxelChunk& center = *neighborhood.center();
    if (center.isUniform() && center.uniformValue() == kEmpty) {
        if (stats) *stats = localStats;
        return mesh; // nothing solid
    }

    const CellGrid grid(neighborhood, table, options.lod, options.sealFaces);
    const VoxelPalette& palette = table.palette();
    const int n = grid.size();
    const float cellSize = static_cast<float>(1 << options.lod) * options.voxelSize;
    std::vector<uint32_t> mask(static_cast<size_t>(n) * n);
    const float uvFactor = cellSize / options.voxelSize;

    for (int face = 0; face < 6; ++face) {
        const int a = face >> 1;
        const bool positive = (face & 1) != 0;
        const int u = (a + 1) % 3;
        const int v = (a + 2) % 3;
        glm::ivec3 aDir(0), uDir(0), vDir(0);
        aDir[a] = positive ? 1 : -1;
        uDir[u] = 1;
        vDir[v] = 1;
        glm::vec3 normal(0.0f);
        normal[a] = positive ? 1.0f : -1.0f;
        glm::vec4 tangent(0.0f, 0.0f, 0.0f, positive ? 1.0f : -1.0f);
        tangent[u] = 1.0f;

        for (int q = 0; q < n; ++q) {
            // Build the face mask for this slice.
            bool any = false;
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < n; ++i) {
                    glm::ivec3 cell;
                    cell[a] = q;
                    cell[u] = i;
                    cell[v] = j;
                    uint32_t key = 0;
                    const glm::ivec3 nbCell = cell + aDir;
                    if ((grid.flags(cell) & BlockInfo::kCube) && !grid.covers(nbCell, face ^ 1)) {
                        uint32_t aoBits = 0xFFu;
                        if (options.ambientOcclusion) {
                            aoBits = 0;
                            const int su[4] = {-1, 1, 1, -1};
                            const int sv[4] = {-1, -1, 1, 1};
                            for (int c = 0; c < 4; ++c) {
                                const bool s1 = grid.occludes(nbCell + uDir * su[c]);
                                const bool s2 = grid.occludes(nbCell + vDir * sv[c]);
                                const bool sc = grid.occludes(nbCell + uDir * su[c] + vDir * sv[c]);
                                const uint32_t ao = (s1 && s2) ? 0u : 3u - (uint32_t(s1) + uint32_t(s2) + uint32_t(sc));
                                aoBits |= ao << (2 * c);
                            }
                        }
                        // Material + 1 keeps the key non-zero for material 0.
                        key = ((static_cast<uint32_t>(table.material(grid.at(cell))) + 1u) << 8) | aoBits;
                        any = true;
                    }
                    mask[static_cast<size_t>(j) * n + i] = key;
                }
            }
            if (!any) {
                continue;
            }

            // Greedy merge.
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < n; ++i) {
                    const uint32_t key = mask[static_cast<size_t>(j) * n + i];
                    if (key == 0) {
                        continue;
                    }
                    const uint32_t aoBits = key & 0xFFu;
                    int w = 1;
                    int h = 1;
                    if (uniformAo(aoBits)) {
                        while (i + w < n && mask[static_cast<size_t>(j) * n + i + w] == key) {
                            ++w;
                        }
                        bool grow = true;
                        while (grow && j + h < n) {
                            for (int k = 0; k < w; ++k) {
                                if (mask[static_cast<size_t>(j + h) * n + i + k] != key) {
                                    grow = false;
                                    break;
                                }
                            }
                            if (grow) {
                                ++h;
                            }
                        }
                    }
                    for (int y = 0; y < h; ++y) {
                        for (int x = 0; x < w; ++x) {
                            mask[static_cast<size_t>(j + y) * n + i + x] = 0;
                        }
                    }
                    ++localStats.quads;
                    localStats.faces += static_cast<uint32_t>(w * h);

                    // Emit the quad: corners (u0,v0), (u1,v0), (u1,v1), (u0,v1).
                    const uint32_t material = (key >> 8) - 1u;
                    const glm::vec4 color = palette.color(material);
                    const int faceKind = a != 1 ? 1 : positive ? 0 : 2;
                    const float layerAlpha = palette.hasLayers() ? palette.layer(material, faceKind) : color.a;
                    const auto base = static_cast<uint32_t>(mesh.vertices.size());
                    const int cu[4] = {i, i + w, i + w, i};
                    const int cv[4] = {j, j, j + h, j + h};
                    uint32_t ao[4];
                    for (int c = 0; c < 4; ++c) {
                        ao[c] = (aoBits >> (2 * c)) & 3u;
                        glm::vec3 p;
                        p[a] = static_cast<float>(q + (positive ? 1 : 0));
                        p[u] = static_cast<float>(cu[c]);
                        p[v] = static_cast<float>(cv[c]);
                        Vertex vert;
                        vert.position = p * cellSize;
                        vert.normal = normal;
                        vert.tangent = tangent;
                        vert.uv0 = glm::vec2(p[u], p[v]) * uvFactor;
                        const float shade = options.aoCurve[ao[c]];
                        vert.color = glm::vec4(glm::vec3(color) * shade, layerAlpha);
                        mesh.vertices.push_back(vert);
                    }
                    // Split along the diagonal with the more similar occlusion.
                    const bool flip = ao[0] + ao[2] < ao[1] + ao[3];
                    if (positive) {
                        if (!flip) mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
                        else mesh.indices.insert(mesh.indices.end(), {base + 1, base + 2, base + 3, base + 1, base + 3, base});
                    } else {
                        if (!flip) mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
                        else mesh.indices.insert(mesh.indices.end(), {base + 1, base + 3, base + 2, base + 1, base, base + 3});
                    }
                }
            }
        }
    }

    if (!mesh.indices.empty()) {
        mesh.submeshes = {{0, static_cast<uint32_t>(mesh.indices.size()), 0, {}}};
        mesh.computeBounds();
    }
    if (stats) *stats = localStats;
    return mesh;
}

} // namespace zodar::voxel
