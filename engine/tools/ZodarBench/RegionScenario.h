#pragma once
#include "Renderer/StreamedMeshSet.h"
#include "Scene/Voxel/RegionStreamer.h"
#include <map>

// Synthetic engine workload, independent of game content. Inline streaming deliberately
// fixes job ordering; timings measure latency, not asynchronous throughput.
class RegionScenario {
public:
    RegionScenario(zodar::gfx::VulkanContext&, zodar::gfx::UploadContext&,
                   zodar::gfx::MaterialHandle, const std::filesystem::path&);
    void update(uint32_t tick, zodar::gfx::SceneView&, std::vector<zodar::gfx::MeshDraw>&);
    void writeReport(std::ostream&) const;
    void flush();
    uint32_t uniqueMeshCount() const { return meshes.stats().meshes + 1; } // shared prop mesh

private:
    std::shared_ptr<zodar::voxel::RegionStreamer> regions;
    std::unique_ptr<zodar::voxel::VoxelWorld> world;
    zodar::gfx::StreamedMeshSet meshes;
    zodar::gfx::MaterialHandle material;
    std::map<zodar::voxel::ChunkCoord, uint64_t> keys;
    bool reloadVerified = false;
    uint64_t nextKey = 1, edits = 0, loads = 0, saves = 0, reloads = 0;
    std::map<zodar::voxel::RegionCoord, bool> visited;
    std::vector<double> updateMs, uploadMs;
    uint64_t peakCpu = 0, peakRegionCpu = 0, peakGpu = 0, peakRegions = 0, peakChunks = 0, unsettled = 0;
};
