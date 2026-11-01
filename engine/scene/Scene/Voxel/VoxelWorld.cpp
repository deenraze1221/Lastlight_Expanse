#include "Scene/Voxel/VoxelWorld.h"

#include "Core/Error.h"
#include "Core/JobSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace zodar::voxel {
namespace {

// Full-cube voxel layers behind each face. Shape cells do not count: they may leave openings.
std::array<uint8_t, 6> computeSolidDepth(const VoxelChunk& c, const BlockTable& table) {
    std::array<uint8_t, 6> out{};
    if (c.isUniform()) {
        out.fill(table.isCube(c.uniformValue()) && c.uniformValue() != kEmpty ? uint8_t(kChunkSize) : uint8_t(0));
        return out;
    }
    for (int f = 0; f < 6; ++f) {
        const int axis = f >> 1;
        const bool positive = (f & 1) != 0;
        int depth = 0;
        for (; depth < kChunkSize; ++depth) {
            const int layer = positive ? kChunkSize - 1 - depth : depth;
            bool full = true;
            for (int a = 0; a < kChunkSize && full; ++a) {
                for (int b = 0; b < kChunkSize && full; ++b) {
                    glm::ivec3 p;
                    p[axis] = layer;
                    p[(axis + 1) % 3] = a;
                    p[(axis + 2) % 3] = b;
                    const Voxel v = c.get(p.x, p.y, p.z);
                    full = v != kEmpty && table.isCube(v);
                }
            }
            if (!full) break;
        }
        out[f] = static_cast<uint8_t>(depth);
    }
    return out;
}

} // namespace
VoxelWorld::VoxelWorld(std::shared_ptr<const IVoxelGenerator> generator, VoxelPalette palette,
                       StreamingSettings settings, JobSystem* jobs)
    : m_generator(std::move(generator)), m_table(std::make_shared<const BlockTable>(palette)),
      m_settings(std::move(settings)), m_jobs(jobs) {
    // Below the world's lowest chunk layer reads as a solid cube, so the underside of the world
    // is never meshed; above the highest layer reads as empty.
    m_bedrock = std::make_shared<const VoxelChunk>(m_table->firstCube());
    ZODAR_ENSURE(m_generator != nullptr, "VoxelWorld needs a generator");
    ZODAR_ENSURE(m_settings.voxelSize > 0.0f, "voxelSize must be positive");
    ZODAR_ENSURE(m_settings.viewRadius >= 0, "viewRadius must not be negative");
    ZODAR_ENSURE(m_settings.minChunkY <= m_settings.maxChunkY, "minChunkY > maxChunkY");
    ZODAR_ENSURE(std::is_sorted(m_settings.lodDistances.begin(), m_settings.lodDistances.end()),
                 "lodDistances must be ascending");
    ZODAR_ENSURE(m_settings.lodDistances.size() <= static_cast<size_t>(kMaxLod), "too many LOD levels");
}

VoxelWorld::~VoxelWorld() {
    // Jobs capture `this`; they must finish before the members go away.
    if (m_jobs) {
        m_jobs->waitIdle();
    }
}

ChunkCoord VoxelWorld::chunkAt(const glm::vec3& p) const {
    const glm::vec3 c = glm::floor(p / chunkWorldSize());
    return {static_cast<int32_t>(c.x), static_cast<int32_t>(c.y), static_cast<int32_t>(c.z)};
}

glm::vec3 VoxelWorld::chunkOrigin(const ChunkCoord& coord) const {
    return glm::vec3(coord.originVoxel()) * m_settings.voxelSize;
}

int VoxelWorld::lodFor(const ChunkCoord& chunk, const glm::vec3& observerChunkSpace) const {
    const glm::vec3 center = glm::vec3(chunk.x, chunk.y, chunk.z) + 0.5f;
    const float d = glm::length(center - observerChunkSpace);
    for (size_t i = 0; i < m_settings.lodDistances.size(); ++i) {
        if (d < m_settings.lodDistances[i]) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(m_settings.lodDistances.size());
}

void VoxelWorld::update(const glm::vec3& observer) {
    integrateResults();

    m_observerChunkSpace = observer / chunkWorldSize();
    const ChunkCoord center = chunkAt(observer);
    if (!m_candidateCenter || *m_candidateCenter != center) {
        rebuildCandidates(center, m_observerChunkSpace);
        unloadFarChunks(m_observerChunkSpace);
    }
    // LODs follow the exact observer position, not just its chunk.
    for (auto& [coord, lod] : m_wantedLod) {
        lod = lodFor(coord, m_observerChunkSpace);
    }

    const float meshRadius = static_cast<float>(m_settings.viewRadius);
    uint32_t budget = m_settings.maxJobsPerUpdate;
    bool settled = true;

    for (const Candidate& c : m_candidates) {
        const bool wantsMesh = c.horizontal <= meshRadius;
        auto it = m_chunks.find(c.coord);
        if (it == m_chunks.end() || (!it->second.data && !it->second.generating)) {
            settled = false;
            if (budget == 0 || m_generateInFlight + m_meshInFlight >= m_settings.maxJobsInFlight) {
                continue;
            }
            --budget;
            Entry& entry = m_chunks[c.coord];
            if (auto edit = m_edits.find(c.coord); edit != m_edits.end()) {
                // Restore player edits instead of regenerating. Neighbour meshes that were built
                // while this chunk was loaded saw the same data, so nothing else is touched.
                entry.data = edit->second;
                entry.solidDepth = computeSolidDepth(*entry.data, *m_table);
                continue;
            }
            entry.generating = true;
            ++m_generateInFlight;
            const ChunkCoord coord = c.coord;
            {
                auto job = [this, coord] {
                    auto data = std::make_shared<VoxelChunk>();
                    m_generator->generate(coord, *data);
                    data->compact();
                    const auto depth = computeSolidDepth(*data, *m_table);
                    std::lock_guard lock(m_resultMutex);
                    m_generateResults.push_back({coord, std::move(data), depth});
                };
                if (m_jobs) m_jobs->submit(std::move(job));
                else job();
            }
            continue;
        }
        if (!wantsMesh) {
            continue;
        }
        Entry& entry = it->second;
        if (!entry.data) {
            settled = false;
            continue;
        }
        const int lod = m_wantedLod.at(c.coord);
        const uint8_t seal = sealMask(c.coord, lod);
        const bool upToDate =
            entry.meshed && entry.meshedLod == lod && entry.meshedSeal == seal && entry.meshedVersion == entry.inputsVersion;
        if (upToDate) {
            continue;
        }
        settled = false;
        if (entry.meshPending || !neighborsReady(c.coord)) {
            continue;
        }
        if (trivialMesh(c.coord)) {
            // Nothing visible (all air, or solid and buried): no job needed.
            if (entry.hasMesh) {
                m_events.push_back({ChunkMeshEvent::Type::Remove, c.coord, lod, chunkOrigin(c.coord), {}});
                entry.hasMesh = false;
            }
            entry.meshed = true;
            entry.meshedLod = lod;
            entry.meshedSeal = seal;
            entry.meshedVersion = entry.inputsVersion;
            continue;
        }
        if (budget == 0 || m_generateInFlight + m_meshInFlight >= m_settings.maxJobsInFlight) {
            continue;
        }
        --budget;
        scheduleMesh(c.coord, entry, lod, seal);
    }
    if (m_generateInFlight + m_meshInFlight > 0) {
        settled = false;
    }
    integrateResults(); // inline mode: results of this update are visible immediately
    m_settled = settled && m_generateInFlight + m_meshInFlight == 0;
}

void VoxelWorld::scheduleMesh(const ChunkCoord& coord, Entry& entry, int lod, uint8_t seal) {
    // Capture shared ownership of the 27 chunks: edits replace chunk pointers (copy on
    // write), so the job reads immutable data without locks.
    std::array<std::shared_ptr<const VoxelChunk>, 27> inputs;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (coord.y + dy < m_settings.minChunkY) {
                    inputs[ChunkNeighborhood::slot(dx, dy, dz)] = m_bedrock;
                    continue;
                }
                auto it = m_chunks.find(coord.offset(dx, dy, dz));
                if (it != m_chunks.end()) {
                    inputs[ChunkNeighborhood::slot(dx, dy, dz)] = it->second.data;
                }
            }
        }
    }
    entry.meshPending = true;
    ++m_meshInFlight;
    MeshOptions options = m_settings.mesh;
    options.lod = lod;
    options.voxelSize = m_settings.voxelSize;
    options.sealFaces = seal;
    const uint64_t version = entry.inputsVersion;
    auto job = [this, coord, inputs = std::move(inputs), options, version] {
        ChunkNeighborhood nb;
        for (size_t i = 0; i < inputs.size(); ++i) {
            nb.chunks[i] = inputs[i].get();
        }
        MeshData mesh = meshChunk(nb, *m_table, options);
        std::lock_guard lock(m_resultMutex);
        m_meshResults.push_back({coord, options.lod, options.sealFaces, version, std::move(mesh)});
    };
    if (m_jobs) m_jobs->submit(std::move(job));
    else job();
}

void VoxelWorld::integrateResults() {
    std::vector<GenerateResult> generated;
    std::vector<MeshResult> meshes;
    {
        std::lock_guard lock(m_resultMutex);
        generated.swap(m_generateResults);
        meshes.swap(m_meshResults);
    }
    // Completion order varies between runs with worker threads; apply in a fixed order.
    const auto byCoord = [](const auto& a, const auto& b) { return a.coord < b.coord; };
    std::sort(generated.begin(), generated.end(), byCoord);
    std::sort(meshes.begin(), meshes.end(), byCoord);

    for (GenerateResult& r : generated) {
        --m_generateInFlight;
        ++m_totalGenerated;
        auto it = m_chunks.find(r.coord);
        if (it == m_chunks.end() || !it->second.generating) {
            continue; // unloaded while generating
        }
        it->second.generating = false;
        it->second.data = std::move(r.data);
        it->second.solidDepth = r.solidDepth;
    }
    for (MeshResult& r : meshes) {
        --m_meshInFlight;
        ++m_totalMeshed;
        auto it = m_chunks.find(r.coord);
        if (it == m_chunks.end()) {
            ++m_staleDropped;
            continue;
        }
        Entry& entry = it->second;
        entry.meshPending = false;
        if (r.inputsVersion != entry.inputsVersion || !m_wantedLod.contains(r.coord)) {
            ++m_staleDropped; // edited or left the view meanwhile; rescheduled if still wanted
            continue;
        }
        entry.meshed = true;
        entry.meshedLod = r.lod;
        entry.meshedSeal = r.seal;
        entry.meshedVersion = r.inputsVersion;
        if (r.mesh.indices.empty()) {
            if (entry.hasMesh) {
                m_events.push_back({ChunkMeshEvent::Type::Remove, r.coord, r.lod, chunkOrigin(r.coord), {}});
                entry.hasMesh = false;
            }
        } else {
            entry.hasMesh = true;
            m_events.push_back({ChunkMeshEvent::Type::Upsert, r.coord, r.lod, chunkOrigin(r.coord), std::move(r.mesh)});
        }
    }
}

void VoxelWorld::rebuildCandidates(const ChunkCoord& center, const glm::vec3& observerChunkSpace) {
    m_candidateCenter = center;
    m_candidates.clear();
    m_wantedLod.clear();
    const int r = m_settings.viewRadius + 1; // +1: neighbours needed for meshing
    const float meshRadius = static_cast<float>(m_settings.viewRadius);
    for (int y = m_settings.minChunkY; y <= m_settings.maxChunkY; ++y) {
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                const ChunkCoord coord{center.x + dx, y, center.z + dz};
                const glm::vec3 c = glm::vec3(coord.x, coord.y, coord.z) + 0.5f;
                const glm::vec2 h = glm::vec2(c.x, c.z) - glm::vec2(observerChunkSpace.x, observerChunkSpace.z);
                const float horizontal = glm::length(h);
                if (horizontal > meshRadius + 1.5f) {
                    continue;
                }
                m_candidates.push_back({coord, glm::length(c - observerChunkSpace), horizontal});
                if (horizontal <= meshRadius) {
                    m_wantedLod[coord] = lodFor(coord, observerChunkSpace);
                }
            }
        }
    }
    std::sort(m_candidates.begin(), m_candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.coord < b.coord;
    });
}

void VoxelWorld::unloadFarChunks(const glm::vec3& observerChunkSpace) {
    const float meshKeep = static_cast<float>(m_settings.viewRadius) + m_settings.unloadHysteresis;
    const float dataKeep = meshKeep + 1.5f;
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        const ChunkCoord& coord = it->first;
        Entry& entry = it->second;
        const glm::vec2 h = glm::vec2(coord.x + 0.5f, coord.z + 0.5f) - glm::vec2(observerChunkSpace.x, observerChunkSpace.z);
        const float horizontal = glm::length(h);
        if (horizontal > meshKeep && (entry.hasMesh || entry.meshed)) {
            if (entry.hasMesh) {
                m_events.push_back({ChunkMeshEvent::Type::Remove, coord, entry.meshedLod, chunkOrigin(coord), {}});
            }
            entry.hasMesh = false;
            entry.meshed = false;
        }
        // Chunks with jobs in flight stay until the job reports back.
        if (horizontal > dataKeep && !entry.generating && !entry.meshPending) {
            it = m_chunks.erase(it);
        } else {
            ++it;
        }
    }
}

bool VoxelWorld::neighborsReady(const ChunkCoord& coord) const {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (!inWorldY(coord.y + dy)) {
                continue; // outside the world: bedrock below, empty above
            }
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = m_chunks.find(coord.offset(dx, dy, dz));
                if (it == m_chunks.end() || !it->second.data) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool VoxelWorld::uniformCube(const VoxelChunk& chunk) const {
    return chunk.isUniform() && chunk.uniformValue() != kEmpty && m_table->isCube(chunk.uniformValue());
}

bool VoxelWorld::trivialMesh(const ChunkCoord& coord) const {
    const VoxelChunk& self = *m_chunks.at(coord).data;
    if (self.isUniform() && self.uniformValue() == kEmpty) {
        return true;
    }
    if (!uniformCube(self)) {
        return false;
    }
    // Solid chunk: invisible only if every face neighbour is solid as well (and none is sealed).
    const int d[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (const auto& o : d) {
        const ChunkCoord n = coord.offset(o[0], o[1], o[2]);
        if (n.y < m_settings.minChunkY) {
            continue; // bedrock
        }
        if (n.y > m_settings.maxChunkY) {
            return false;
        }
        const auto it = m_chunks.find(n);
        if (it == m_chunks.end() || !it->second.data || !uniformCube(*it->second.data)) {
            return false;
        }
    }
    return sealMask(coord, m_wantedLod.at(coord)) == 0;
}

uint8_t VoxelWorld::sealMask(const ChunkCoord& coord, int lod) const {
    const int d[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    uint8_t mask = 0;
    for (int f = 0; f < 6; ++f) {
        const ChunkCoord n = coord.offset(d[f][0], d[f][1], d[f][2]);
        const auto it = m_wantedLod.find(n);
        if (it == m_wantedLod.end() || it->second == lod) {
            continue;
        }
        // No crack is possible if the neighbour is all air (empty at every LOD), or if its
        // layers next to us are solid at least one coarse cell deep (solid at every LOD).
        const auto data = m_chunks.find(n);
        bool needed = true;
        if (data != m_chunks.end() && data->second.data) {
            const VoxelChunk& nc = *data->second.data;
            const int opposite = f ^ 1; // the neighbour's face that touches us
            const int reach = 1 << std::max(lod, it->second);
            needed = !(nc.isUniform() && nc.uniformValue() == kEmpty) && data->second.solidDepth[opposite] < reach;
        }
        if (needed) {
            mask |= static_cast<uint8_t>(1u << f);
        }
    }
    return mask;
}

void VoxelWorld::touch(const ChunkCoord& coord, const glm::ivec3& local) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const ChunkCoord n = coord.offset(dx, dy, dz);
                auto it = m_chunks.find(n);
                if (it == m_chunks.end()) {
                    continue;
                }
                // A neighbour on side d reads `reach` voxels of this chunk next to the shared
                // boundary: one at LOD 0, a whole coarse cell (2^lod) otherwise.
                const auto wanted = m_wantedLod.find(n);
                const int reach = 1 << (wanted != m_wantedLod.end() ? std::max(wanted->second, it->second.meshedLod) : kMaxLod);
                const int d[3] = {dx, dy, dz};
                bool affected = true;
                for (int axis = 0; axis < 3 && affected; ++axis) {
                    if (d[axis] < 0) affected = local[axis] < reach;
                    else if (d[axis] > 0) affected = local[axis] >= kChunkSize - reach;
                }
                if (affected) {
                    ++it->second.inputsVersion;
                }
            }
        }
    }
}

std::vector<ChunkMeshEvent> VoxelWorld::takeMeshEvents() {
    std::vector<ChunkMeshEvent> out;
    out.swap(m_events);
    return out;
}

void VoxelWorld::finishPendingWork() {
    if (m_jobs) {
        m_jobs->waitIdle();
    }
    integrateResults();
}

Voxel VoxelWorld::getVoxel(const glm::ivec3& voxel) const {
    const auto it = m_chunks.find(chunkOf(voxel));
    if (it == m_chunks.end() || !it->second.data) {
        return kEmpty;
    }
    const glm::ivec3 l = localOf(voxel);
    return it->second.data->get(l.x, l.y, l.z);
}

bool VoxelWorld::setVoxel(const glm::ivec3& voxel, Voxel value) {
    const ChunkCoord coord = chunkOf(voxel);
    auto it = m_chunks.find(coord);
    if (it == m_chunks.end() || !it->second.data) {
        return false;
    }
    const glm::ivec3 l = localOf(voxel);
    if (it->second.data->get(l.x, l.y, l.z) == value) {
        return true;
    }
    auto copy = std::make_shared<VoxelChunk>(*it->second.data); // copy on write: jobs may read the old one
    copy->set(l.x, l.y, l.z, value);
    copy->compact();
    it->second.data = copy;
    it->second.solidDepth = computeSolidDepth(*copy, *m_table);
    m_edits[coord] = copy;
    // Neighbours read this chunk's boundary for faces, ambient occlusion and (at coarse LOD)
    // downsampled padding cells, so they are rebuilt when the voxel is within reach.
    touch(coord, l);
    m_settled = false;
    return true;
}

QueryStatus VoxelWorld::collectSolidBoxes(const WorldBox& query, std::vector<SolidBox>& out) const {
    for (int i = 0; i < 3; ++i) {
        ZODAR_ENSURE(std::isfinite(query.min[i]) && std::isfinite(query.max[i]),
                     "collectSolidBoxes: query box has a non-finite coordinate");
        ZODAR_ENSURE(query.min[i] <= query.max[i], "collectSolidBoxes: query box has min > max");
    }
    const float vs = m_settings.voxelSize;
    for (int i = 0; i < 3; ++i) {
        ZODAR_ENSURE(std::abs(query.min[i] / vs) < 1.0e9f && std::abs(query.max[i] / vs) < 1.0e9f,
                     "collectSolidBoxes: query box lies outside the representable cell range");
    }
    // Cells [c, c + 1] * vs touching [min, max]: c in [ceil(min / vs) - 1, floor(max / vs)].
    // Each candidate is re-checked against the exact world box below, so rounding here only
    // has to be conservative.
    glm::ivec3 lo;
    glm::ivec3 hi;
    for (int i = 0; i < 3; ++i) {
        lo[i] = static_cast<int>(std::ceil(query.min[i] / vs)) - 1;
        hi[i] = static_cast<int>(std::floor(query.max[i] / vs));
    }
    const auto touches = [&](const glm::vec3& bmin, const glm::vec3& bmax) {
        return bmin.x <= query.max.x && bmin.y <= query.max.y && bmin.z <= query.max.z && bmax.x >= query.min.x &&
               bmax.y >= query.min.y && bmax.z >= query.min.z;
    };
    const BlockTable& table = *m_table;
    QueryStatus status = QueryStatus::Complete;
    const ChunkCoord c0 = chunkOf(lo);
    const ChunkCoord c1 = chunkOf(hi);
    for (int cy = c0.y; cy <= c1.y; ++cy) {
        if (!inWorldY(cy)) continue; // reads as empty (getVoxel), not missing
        for (int cz = c0.z; cz <= c1.z; ++cz) {
            for (int cx = c0.x; cx <= c1.x; ++cx) {
                const ChunkCoord coord{cx, cy, cz};
                const auto it = m_chunks.find(coord);
                if (it == m_chunks.end() || !it->second.data) {
                    status = QueryStatus::MissingChunks;
                    continue;
                }
                const VoxelChunk& chunk = *it->second.data;
                const glm::ivec3 origin = coord.originVoxel();
                const glm::ivec3 a = glm::max(lo, origin) - origin;
                const glm::ivec3 b = glm::min(hi, origin + glm::ivec3(kChunkSize - 1)) - origin;
                if (chunk.isUniform() && !table.collides(chunk.uniformValue())) continue;
                for (int y = a.y; y <= b.y; ++y) {
                    for (int z = a.z; z <= b.z; ++z) {
                        for (int x = a.x; x <= b.x; ++x) {
                            const Voxel v = chunk.get(x, y, z);
                            if (!table.collides(v)) continue;
                            const glm::vec3 cell(origin + glm::ivec3(x, y, z));
                            if (table.isCube(v)) {
                                int end = x;
                                while (end < b.x && chunk.get(end + 1, y, z) == v) ++end;
                                const SolidBox box{cell * vs, (cell + glm::vec3(float(end - x + 1), 1.0f, 1.0f)) * vs, v};
                                if (touches(box.min, box.max)) out.push_back(box);
                                x = end;
                                continue;
                            }
                            const Shape* shape = table.shape(v);
                            if (!shape) continue;
                            for (const ShapeBox& sb : shape->collision) {
                                const SolidBox box{(cell + sb.min) * vs, (cell + sb.max) * vs, v};
                                if (touches(box.min, box.max)) out.push_back(box);
                            }
                        }
                    }
                }
            }
        }
    }
    return status;
}

namespace {

// Dominant axis of a normal as an integer face normal (ties prefer Y, then X: the mesher's rule).
glm::ivec3 dominantFace(const glm::vec3& n) {
    const glm::vec3 a = glm::abs(n);
    const int axis = (a.y >= a.x && a.y >= a.z) ? 1 : a.x >= a.z ? 0 : 2;
    glm::ivec3 f(0);
    f[axis] = n[axis] > 0.0f ? 1 : -1;
    return f;
}

struct LocalHit {
    float t = 0.0f;
    glm::vec3 normal{0.0f};
};

constexpr float kGrazing = 1e-7f;  // |dot(dir, n)| below this: the ray runs along the polygon
constexpr float kEdgeEps = 1e-6f;  // barycentric tolerance: shared edges never leak
constexpr float kCellEps = 1e-5f;  // hits this close outside the cell's ray interval still count

// Front faces of the shape's polygons along o + t d (cell-local, voxel units) for t in
// [t0, t1]. Strict `<` keeps the lowest polygon / triangle index on ties.
std::optional<LocalHit> polygonHit(const Shape& shape, const glm::vec3& o, const glm::vec3& d, float t0, float t1) {
    std::optional<LocalHit> best;
    for (const Shape::Polygon& poly : shape.polygons) {
        const float facing = glm::dot(d, poly.normal);
        if (facing > -kGrazing) continue; // back face or grazing
        const glm::vec3& a = poly.corners[0];
        for (size_t k = 1; k + 1 < poly.corners.size(); ++k) {
            const glm::vec3 e1 = poly.corners[k] - a;
            const glm::vec3 e2 = poly.corners[k + 1] - a;
            const glm::vec3 q = glm::cross(d, e2);
            const float det = glm::dot(e1, q);
            if (det == 0.0f) continue;
            const float inv = 1.0f / det;
            const glm::vec3 s = o - a;
            const float u = glm::dot(s, q) * inv;
            if (u < -kEdgeEps || u > 1.0f + kEdgeEps) continue;
            const glm::vec3 r = glm::cross(s, e1);
            const float v = glm::dot(d, r) * inv;
            if (v < -kEdgeEps || u + v > 1.0f + kEdgeEps) continue;
            const float t = glm::dot(e2, r) * inv;
            if (t < t0 - kCellEps || t > t1 + kCellEps) continue;
            if (!best || t < best->t) best = LocalHit{t, poly.normal};
        }
    }
    return best;
}

// Entry into the shape's collision boxes (slab test), cell-local, for t in [t0, t1].
std::optional<LocalHit> boxHit(const Shape& shape, const glm::vec3& o, const glm::vec3& d, float t0, float t1) {
    std::optional<LocalHit> best;
    for (const ShapeBox& b : shape.collision) {
        float tNear = -std::numeric_limits<float>::infinity();
        float tFar = std::numeric_limits<float>::infinity();
        int axis = -1;
        bool miss = false;
        for (int i = 0; i < 3 && !miss; ++i) {
            if (d[i] == 0.0f) {
                miss = o[i] < b.min[i] || o[i] > b.max[i];
                continue;
            }
            const float inv = 1.0f / d[i];
            float ta = (b.min[i] - o[i]) * inv;
            float tb = (b.max[i] - o[i]) * inv;
            if (ta > tb) std::swap(ta, tb);
            if (ta > tNear) {
                tNear = ta;
                axis = i;
            }
            tFar = std::min(tFar, tb);
        }
        if (miss || axis < 0 || tNear > tFar || tNear < t0 - kCellEps || tNear > t1 + kCellEps) continue;
        if (!best || tNear < best->t) {
            glm::vec3 n(0.0f);
            n[axis] = d[axis] > 0.0f ? -1.0f : 1.0f;
            best = LocalHit{tNear, n};
        }
    }
    return best;
}

bool insideBoxes(const Shape& shape, const glm::vec3& p) {
    for (const ShapeBox& b : shape.collision) {
        if (glm::all(glm::greaterThanEqual(p, b.min)) && glm::all(glm::lessThanEqual(p, b.max))) return true;
    }
    return false;
}

} // namespace

std::optional<RaycastHit> VoxelWorld::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                              const RaycastOptions& options) const {
    const float len = glm::length(direction);
    if (len <= 0.0f || maxDistance <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 dir = direction / len;
    const glm::vec3 p = origin / m_settings.voxelSize; // voxel space
    const float maxT = maxDistance / m_settings.voxelSize;
    glm::ivec3 cell(glm::floor(p));
    glm::ivec3 step;
    glm::vec3 tMax;
    glm::vec3 tDelta;
    for (int i = 0; i < 3; ++i) {
        if (dir[i] > 0.0f) {
            step[i] = 1;
            tDelta[i] = 1.0f / dir[i];
            tMax[i] = (static_cast<float>(cell[i]) + 1.0f - p[i]) * tDelta[i];
        } else if (dir[i] < 0.0f) {
            step[i] = -1;
            tDelta[i] = -1.0f / dir[i];
            tMax[i] = (p[i] - static_cast<float>(cell[i])) * tDelta[i];
        } else {
            step[i] = 0;
            tDelta[i] = std::numeric_limits<float>::infinity();
            tMax[i] = std::numeric_limits<float>::infinity();
        }
    }
    const BlockTable& table = *m_table;
    const bool collision = options.mode == RaycastMode::Collision;
    const glm::ivec3 startCell = cell;
    glm::ivec3 normal(0);
    float t = 0.0f;
    while (t <= maxT) {
        const Voxel v = getVoxel(cell);
        const Shape* shape = v != kEmpty && !table.isCube(v) ? table.shape(v) : nullptr;
        if (v != kEmpty && (!collision || table.collides(v)) && !shape) {
            // Cube cell: unchanged pre-V3 result; point and surface normal derived from it.
            const float distance = t * m_settings.voxelSize;
            return RaycastHit{cell, normal, distance, v, origin + dir * distance, glm::vec3(normal)};
        }
        if (shape && (!collision || table.collides(v))) {
            const glm::vec3 local = p - glm::vec3(cell);
            const bool start = cell == startCell;
            if (start && (collision ? insideBoxes(*shape, local) : shape->contains(local))) {
                return RaycastHit{cell, glm::ivec3(0), 0.0f, v, origin, glm::vec3(0.0f)};
            }
            const float tExit = std::min(tMax.x, std::min(tMax.y, tMax.z));
            const auto hit = collision ? boxHit(*shape, local, dir, t, tExit) : polygonHit(*shape, local, dir, t, tExit);
            if (hit) {
                const float th = std::max(hit->t, 0.0f);
                if (th <= maxT) {
                    const float distance = th * m_settings.voxelSize;
                    return RaycastHit{cell, dominantFace(hit->normal), distance, v, origin + dir * distance, hit->normal};
                }
                return std::nullopt;
            }
        }
        const int axis = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
        t = tMax[axis];
        tMax[axis] += tDelta[axis];
        cell[axis] += step[axis];
        normal = glm::ivec3(0);
        normal[axis] = -step[axis];
    }
    return std::nullopt;
}

VoxelWorldStats VoxelWorld::stats() const {
    VoxelWorldStats s;
    for (const auto& [coord, entry] : m_chunks) {
        if (entry.data) {
            ++s.chunksWithData;
            s.voxelBytes += entry.data->memoryBytes();
        }
        if (entry.hasMesh) {
            ++s.chunksMeshed;
            s.lodHistogram[std::clamp(entry.meshedLod, 0, kMaxLod)]++;
        }
    }
    s.generateInFlight = m_generateInFlight;
    s.meshInFlight = m_meshInFlight;
    s.editedChunks = static_cast<uint32_t>(m_edits.size());
    s.generated = m_totalGenerated;
    s.meshed = m_totalMeshed;
    s.staleMeshesDropped = m_staleDropped;
    return s;
}

} // namespace zodar::voxel
