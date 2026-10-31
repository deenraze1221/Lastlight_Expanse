#include "Scene/Voxel/VoxelWorld.h"

#include "Core/Error.h"
#include "Core/JobSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace zodar::voxel {
namespace {

// Below the world's lowest chunk layer reads as solid, so the underside of the world is
// never meshed; above the highest layer reads as empty.
const std::shared_ptr<const VoxelChunk>& bedrock() {
    static const auto chunk = std::make_shared<const VoxelChunk>(Voxel(1));
    return chunk;
}

std::array<uint8_t, 6> computeSolidDepth(const VoxelChunk& c) {
    std::array<uint8_t, 6> out{};
    if (c.isUniform()) {
        out.fill(c.uniformValue() != kEmpty ? uint8_t(kChunkSize) : uint8_t(0));
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
                    full = c.get(p.x, p.y, p.z) != kEmpty;
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
    : m_generator(std::move(generator)), m_palette(std::move(palette)), m_settings(std::move(settings)), m_jobs(jobs) {
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
                entry.solidDepth = computeSolidDepth(*entry.data);
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
                    const auto depth = computeSolidDepth(*data);
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
                    inputs[ChunkNeighborhood::slot(dx, dy, dz)] = bedrock();
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
        MeshData mesh = meshChunk(nb, m_palette, options);
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

bool VoxelWorld::trivialMesh(const ChunkCoord& coord) const {
    const VoxelChunk& self = *m_chunks.at(coord).data;
    if (self.isUniform() && self.uniformValue() == kEmpty) {
        return true;
    }
    if (!self.isUniform() || !self.isFull()) {
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
        if (it == m_chunks.end() || !it->second.data || !it->second.data->isUniform() || !it->second.data->isFull()) {
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
    it->second.solidDepth = computeSolidDepth(*copy);
    m_edits[coord] = copy;
    // Neighbours read this chunk's boundary for faces, ambient occlusion and (at coarse LOD)
    // downsampled padding cells, so they are rebuilt when the voxel is within reach.
    touch(coord, l);
    m_settled = false;
    return true;
}

std::optional<RaycastHit> VoxelWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                              float maxDistance) const {
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
    glm::ivec3 normal(0);
    float t = 0.0f;
    while (t <= maxT) {
        const Voxel v = getVoxel(cell);
        if (v != kEmpty) {
            return RaycastHit{cell, normal, t * m_settings.voxelSize, v};
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
