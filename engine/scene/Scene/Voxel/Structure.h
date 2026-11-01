#pragma once
#include "Scene/Voxel/Region.h"
#include <functional>
namespace zodar::voxel {
struct StructureAnchor {
    std::string name;
    glm::ivec3 cell{0};
};
struct StructureModel {
    std::string asset;
    glm::vec3 position{0}; // cell units relative to origin
    uint8_t yaw = 0;
};
struct Structure {
    glm::ivec3 size{1}, origin{0};
    std::map<ChunkCoord, std::string> cells; // local cell coordinates, including explicit air
    std::vector<StructureAnchor> anchors;
    std::vector<StructureModel> models;
};
Structure captureStructure(const VoxelWorld& world, glm::ivec3 min, glm::ivec3 size, const BlockNames& names);
Structure rotateStructure(const Structure& source, uint8_t yaw, const BlockNames& names);
// Returns only cells in this region; callers stamp all intersected regions before publication.
void stampStructure(RegionData& region, const Structure& structure, glm::ivec3 position, bool runtime = false);
std::string serializeStructure(const Structure& structure);
Structure parseStructure(std::string_view bytes);
// Immutable generator composition; order is significant, later stamps win.
struct StructureStamp { Structure structure; glm::ivec3 position{0}; };
class LayeredGenerator final : public IVoxelGenerator {
public:
    LayeredGenerator(std::shared_ptr<const IVoxelGenerator> base, BlockNames names,
                     std::vector<StructureStamp> stamps = {}, RegionLayer authored = {}, RegionLayer runtime = {});
    void generate(const ChunkCoord& coord, VoxelChunk& out) const override;
private:
    std::shared_ptr<const IVoxelGenerator> m_base;
    BlockNames m_names;
    RegionLayer m_structures, m_authored, m_runtime;
};
} // namespace zodar::voxel
