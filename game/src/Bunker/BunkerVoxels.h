#pragma once
#include "SceneFile/BlockFile.h"
#include "Scene/Voxel/VoxelWorld.h"
#include <filesystem>
#include <memory>

namespace lastlight {
// Static authored V5 slice. All bunker chunks remain loaded at LOD0 around a fixed observer.
class BunkerVoxels {
public:
    explicit BunkerVoxels(const std::filesystem::path& assets, zodar::JobSystem* jobs = nullptr);
    zodar::BlockCatalogue catalogue;
    std::unique_ptr<zodar::voxel::VoxelWorld> world;
};
}
