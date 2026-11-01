#pragma once

#include "Assets/MeshData.h"
#include "Scene/Voxel/ChunkMesher.h"
#include "Scene/Voxel/VoxelChunk.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace zodar {
class JobSystem;
}

namespace zodar::voxel {

// Fills one chunk. Called from worker threads concurrently, so implementations must be
// thread-safe (const, no shared mutable state) and deterministic for a given coordinate.
class IVoxelGenerator {
public:
    virtual ~IVoxelGenerator() = default;
    virtual void generate(const ChunkCoord& coord, VoxelChunk& out) const = 0;
};

struct StreamingSettings {
    float voxelSize = 1.0f;     // world units per voxel
    int viewRadius = 8;         // chunks around the observer (XZ distance) that get meshes
    int minChunkY = -2;         // vertical world extent in chunks (inclusive)
    int maxChunkY = 3;
    // LOD by 3D distance from the observer to the chunk centre, in chunks:
    // distance < lodDistances[i] selects LOD i; farther chunks use lodDistances.size().
    std::vector<float> lodDistances{3.0f, 6.0f, 12.0f};
    // Conservative LOD (VOXELS §5): chunks containing a block with BlockState::kStructural use
    // LOD 0 while their centre is closer than this (3D, chunks); farther away they follow
    // lodDistances. 0 (default) disables it: LOD choices are exactly the distance rule.
    float structuralLod0Distance = 0.0f;
    float unloadHysteresis = 1.5f;      // extra chunks before meshes/data are dropped
    uint32_t maxJobsPerUpdate = 48;     // generation + meshing jobs scheduled per update()
    uint32_t maxJobsInFlight = 256;
    MeshOptions mesh;                   // lod, voxelSize and sealFaces are set per chunk
};

// Main-thread notification that a chunk's renderable mesh changed. Apply in order.
struct ChunkMeshEvent {
    enum class Type { Upsert, Remove };
    Type type = Type::Upsert;
    ChunkCoord coord;
    int lod = 0;
    glm::vec3 origin{0.0f}; // world position of the chunk's minimum corner (mesh transform)
    MeshData mesh;          // Upsert only; never empty
};

struct RaycastHit {
    glm::ivec3 voxel{0};  // solid voxel that was hit
    // Cube cells: the face that was entered (empty neighbour = voxel + normal). Shape cells: the
    // dominant axis of the surface normal (ties prefer Y, then X), so voxel + normal is the
    // cell a block would be placed in. (0, 0, 0) when the ray starts inside the solid.
    glm::ivec3 normal{0};
    float distance = 0.0f; // world units along the normalised direction
    Voxel value = kEmpty;
    glm::vec3 point{0.0f};         // world position of the hit (origin + direction * distance)
    glm::vec3 surfaceNormal{0.0f}; // unit normal of the surface hit; (0, 0, 0) when starting inside
};

enum class RaycastMode {
    // Every non-empty cell, shapes by their exact polygons (what you see is what you hit).
    // Cells with BlockState::kNoCollision are hit (grates stay selectable).
    Selection,
    // Cells that collide: kNoCollision cells are skipped, shapes are hit by their collision
    // boxes (the same boxes as collectSolidBoxes).
    Collision,
};

struct RaycastOptions {
    RaycastMode mode = RaycastMode::Selection;
};

// World-space axis-aligned box (world units, min <= max on every axis).
struct WorldBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// Solid collision box returned by VoxelWorld::collectSolidBoxes (world units).
struct SolidBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    Voxel value = kEmpty; // block id of the cell(s)
};

enum class QueryStatus {
    Complete,      // every chunk the query touches had data
    MissingChunks, // some chunk inside the world's Y range had no data (not loaded / generating)
};

struct VoxelWorldStats {
    uint32_t chunksWithData = 0;
    uint32_t chunksMeshed = 0;     // chunks currently represented by a non-empty mesh
    uint32_t generateInFlight = 0;
    uint32_t meshInFlight = 0;
    uint32_t editedChunks = 0;
    uint64_t voxelBytes = 0;       // dense chunk arrays (uniform chunks cost nothing)
    uint32_t lodHistogram[kMaxLod + 1]{};
    uint64_t generated = 0;        // totals since creation
    uint64_t meshed = 0;
    uint64_t staleMeshesDropped = 0;
};

// Streams a voxel world around an observer: generates chunk data on worker threads,
// meshes chunks with distance-based LOD (crack-free: boundaries between different LODs are
// sealed with walls), unloads distant chunks with hysteresis and keeps player edits in memory
// so unloaded edited chunks come back unchanged. Work is prioritised nearest-first and
// bounded per update. Main-thread API; the generator runs on JobSystem workers.
//
// With a JobSystem of zero workers (or nullptr) all work runs inline inside update(), and the
// sequence of mesh events is fully deterministic.
class VoxelWorld {
public:
    VoxelWorld(std::shared_ptr<const IVoxelGenerator> generator, VoxelPalette palette, StreamingSettings settings,
               JobSystem* jobs = nullptr);
    ~VoxelWorld();

    VoxelWorld(const VoxelWorld&) = delete;
    VoxelWorld& operator=(const VoxelWorld&) = delete;

    // Integrates finished jobs, then schedules new generation/meshing work around `observer`.
    void update(const glm::vec3& observer);
    // Mesh changes since the last call, in the order they must be applied.
    std::vector<ChunkMeshEvent> takeMeshEvents();
    // Blocks until all scheduled jobs are done and integrates them (tests, loading screens).
    void finishPendingWork();
    // True when every chunk in range has data and an up-to-date mesh.
    bool isSettled() const { return m_settled; }

    // Voxel access in world voxel coordinates. Unloaded chunks read as empty.
    Voxel getVoxel(const glm::ivec3& voxel) const;
    // Edits a loaded voxel; returns false if its chunk has no data. Affected meshes
    // (including neighbours sharing the boundary) are rebuilt by later updates.
    bool setVoxel(const glm::ivec3& voxel, Voxel value);
    // Collision boxes of every solid cell whose cell box touches `query` (closed intervals:
    // touching counts), in world units. Cube cells are merged into runs of equal ids along X
    // (within one chunk, clipped to the query's cell range); shape cells contribute their rotated
    // collision boxes that touch the query. Cells with BlockState::kNoCollision are skipped;
    // ids beyond the registry are cubes. Order is deterministic: chunks by (y, z, x), cells by
    // (y, z, x), then box index.
    //
    // Appends to `out` (never clears it). Returns MissingChunks when a chunk inside the world's
    // Y range has no data: `out` then still holds the boxes of every loaded chunk (valid, in
    // the same order), and the missing chunks contribute nothing; the caller decides whether
    // partial data is usable. Chunks outside the Y range read as empty, like getVoxel, and are
    // not missing. Throws zodar::Error when `query` has a NaN/infinite coordinate or min > max.
    QueryStatus collectSolidBoxes(const WorldBox& query, std::vector<SolidBox>& out) const;

    // First solid voxel along a ray through loaded chunks (Amanatides-Woo traversal; unloaded
    // chunks read as empty). Cube cells are hit when the ray enters them, exactly as before V3.
    // Shape cells (docs/guide/VOXELS.md §6):
    //  - Selection: front faces of the rotated polygons only (back faces and faces within
    //    1e-7 of parallel to the ray are ignored); a hit counts inside the cell's traversal
    //    interval (+-1e-5 cell) and within maxDistance. Nearest hit wins; ties prefer the lower
    //    polygon index, then the lower fan triangle.
    //  - Collision: the rotated collision boxes (slab test); ties prefer the entry axis X, Y,
    //    Z, then the lower box index.
    // A ray that starts inside a cell's solid (cube, Shape::contains, or the interior of the
    // collision boxes) hits that cell at distance 0 with normal (0, 0, 0); one that starts in the
    // empty part of a shape cell continues. Collision mode, origin exactly on the outer face of
    // a collision box: like a cube face, an inward ray hits at distance 0 with that face's
    // normal and an outward ray leaves (faces shared by two boxes count as inside).
    // Throws zodar::Error for a non-finite origin, direction or maxDistance, a negative
    // maxDistance, or an origin / maxDistance beyond 1e9 cells. A zero direction or
    // maxDistance 0 gives no hit.
    std::optional<RaycastHit> raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                                      const RaycastOptions& options = {}) const;

    ChunkCoord chunkAt(const glm::vec3& worldPosition) const;
    glm::vec3 chunkOrigin(const ChunkCoord& coord) const;
    float chunkWorldSize() const { return m_settings.voxelSize * static_cast<float>(kChunkSize); }
    // Distance LOD a chunk would use for an observer in `observerChunk` (exposed for tests and
    // tools). Structural chunks may use a finer LOD (StreamingSettings::structuralLod0Distance).
    int lodFor(const ChunkCoord& chunk, const glm::vec3& observerChunkSpace) const;

    const StreamingSettings& settings() const { return m_settings; }
    const VoxelPalette& palette() const { return m_table->palette(); }
    const BlockTable& blocks() const { return *m_table; }
    VoxelWorldStats stats() const;

private:
    struct Entry {
        std::shared_ptr<const VoxelChunk> data; // null while generating
        bool generating = false;
        uint64_t inputsVersion = 0; // bumped when this chunk or a neighbour it reads is edited
        // Mesh request in flight.
        bool meshPending = false;
        // What the current mesh (or the empty result) was built from.
        bool meshed = false;
        bool hasMesh = false; // a non-empty mesh is live on the renderer side
        int meshedLod = -1;
        uint8_t meshedSeal = 0;
        uint64_t meshedVersion = 0;
        // Full-cube voxel layers behind each face (Face order), 0..32. Lets sealing skip
        // boundaries that no LOD can open up.
        std::array<uint8_t, 6> solidDepth{};
        bool structural = false; // contains a BlockState::kStructural block
    };
    struct Candidate {
        ChunkCoord coord;
        float distance; // 3D, chunk units, from the observer to the chunk centre
        float horizontal;
    };
    struct GenerateResult {
        ChunkCoord coord;
        std::shared_ptr<VoxelChunk> data;
        std::array<uint8_t, 6> solidDepth;
        bool structural;
    };
    struct MeshResult {
        ChunkCoord coord;
        int lod;
        uint8_t seal;
        uint64_t inputsVersion;
        MeshData mesh;
    };

    void integrateResults();
    void rebuildCandidates(const ChunkCoord& center, const glm::vec3& observerChunkSpace);
    void unloadFarChunks(const glm::vec3& observerChunkSpace);
    bool neighborsReady(const ChunkCoord& coord) const;
    int wantedLod(const ChunkCoord& coord) const; // lodFor + conservative LOD for structural chunks
    bool trivialMesh(const ChunkCoord& coord) const;
    bool uniformCube(const VoxelChunk& chunk) const; // uniform, non-empty, full cube
    uint8_t sealMask(const ChunkCoord& coord, int lod) const;
    void scheduleMesh(const ChunkCoord& coord, Entry& entry, int lod, uint8_t seal);
    void touch(const ChunkCoord& coord, const glm::ivec3& local);
    bool inWorldY(int y) const { return y >= m_settings.minChunkY && y <= m_settings.maxChunkY; }

    std::shared_ptr<const IVoxelGenerator> m_generator;
    std::shared_ptr<const BlockTable> m_table; // immutable; read by mesh jobs
    std::shared_ptr<const VoxelChunk> m_bedrock;
    StreamingSettings m_settings;
    JobSystem* m_jobs;

    std::unordered_map<ChunkCoord, Entry, ChunkCoordHash> m_chunks;
    std::unordered_map<ChunkCoord, std::shared_ptr<const VoxelChunk>, ChunkCoordHash> m_edits;
    std::vector<Candidate> m_candidates; // sorted nearest first
    std::unordered_map<ChunkCoord, int, ChunkCoordHash> m_wantedLod; // meshed set -> LOD
    std::optional<ChunkCoord> m_candidateCenter;
    glm::vec3 m_observerChunkSpace{0.0f};
    std::vector<ChunkMeshEvent> m_events;
    bool m_settled = false;

    std::mutex m_resultMutex;
    std::vector<GenerateResult> m_generateResults;
    std::vector<MeshResult> m_meshResults;

    uint32_t m_generateInFlight = 0;
    uint32_t m_meshInFlight = 0;
    uint64_t m_totalGenerated = 0;
    uint64_t m_totalMeshed = 0;
    uint64_t m_staleDropped = 0;
};

} // namespace zodar::voxel
