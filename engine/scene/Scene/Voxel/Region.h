#pragma once
#include "Scene/Voxel/VoxelWorld.h"
#include <filesystem>
#include <map>
#include <string>

namespace zodar::voxel {
// Region coordinates are horizontal; all configured chunk Y layers belong to one region.
struct RegionCoord {
    int32_t x = 0, z = 0;
    auto operator<=>(const RegionCoord&) const = default;
};
struct RegionLayout {
    explicit RegionLayout(float voxelSize);
    int columns = 0;
    RegionCoord regionOf(ChunkCoord chunk) const;
};
// Sparse overrides preserve inheritance and explicit air (the empty name).
using NamedCells = std::map<uint32_t, std::string>;
using RegionLayer = std::map<ChunkCoord, NamedCells>;
struct RegionData {
    RegionCoord coord;
    float voxelSize = 0.5f;
    int32_t minY = 0, maxY = 0;
    std::string baseIdentity;
    RegionLayer authored, runtime;
    bool operator==(const RegionData&) const = default;
};
uint64_t regionMemoryBytes(const RegionData& data);
// Canonical versioned palette/RLE format; all decode allocations are bounded.
std::string serializeRegion(const RegionData& data);
RegionData parseRegion(std::string_view bytes, uint64_t maxMemory = 128 * 1024 * 1024);
RegionData loadRegion(const std::filesystem::path& path, uint64_t maxMemory = 128 * 1024 * 1024);
void saveRegion(const std::filesystem::path& path, const RegionData& data);
// Resolve the entire layer before publishing any data. Unknown names are errors.
using BlockNames = std::vector<std::string>; // id -> stable name, id 0 must be empty
void applyRegionLayer(const RegionLayer& layer, ChunkCoord coord, VoxelChunk& chunk, const BlockNames& names);
} // namespace zodar::voxel
