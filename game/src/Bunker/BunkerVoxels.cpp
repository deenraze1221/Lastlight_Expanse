#include "Bunker/BunkerVoxels.h"
#include "Scene/Voxel/Structure.h"
#include "Core/Error.h"
#include <fstream>
#include <iterator>

namespace lastlight {
namespace {
class Empty final : public zodar::voxel::IVoxelGenerator {
public:
    void generate(const zodar::voxel::ChunkCoord&, zodar::voxel::VoxelChunk& out) const override {
        out = zodar::voxel::VoxelChunk{};
    }
};
}
BunkerVoxels::BunkerVoxels(const std::filesystem::path& assets, zodar::JobSystem* jobs)
    : catalogue(zodar::loadBlockCatalogue(assets / "blocks/bunker_blocks.blocks.json")) {
    using namespace zodar::voxel;
    std::ifstream input(assets / "structures/bunker.zstruct", std::ios::binary);
    ZODAR_ENSURE(input.is_open(), "cannot open bunker structure");
    const std::string bytes{std::istreambuf_iterator<char>(input), {}};
    auto structure = parseStructure(bytes);
    StreamingSettings settings;
    settings.voxelSize = 0.5f;
    settings.viewRadius = 4;
    settings.minChunkY = -1;
    settings.maxChunkY = 0;
    settings.lodDistances = {1000.0f};
    settings.mesh.uvPerMetre = catalogue.uvPerMetre;
    const glm::vec3 observer{24, 1, 0};
    const float chunkSize = settings.voxelSize * kChunkSize;
    const auto first = chunkOf(-structure.origin);
    const auto last = chunkOf(structure.size - structure.origin - glm::ivec3(1));
    ZODAR_ENSURE(first.y >= settings.minChunkY && last.y <= settings.maxChunkY,
                 "bunker structure exceeds preloaded vertical bounds");
    for (int z = first.z; z <= last.z; ++z) for (int x = first.x; x <= last.x; ++x) {
        const glm::vec2 offset(x + .5f - observer.x / chunkSize, z + .5f - observer.z / chunkSize);
        ZODAR_ENSURE(glm::length(offset) <= settings.viewRadius,
                     "bunker structure exceeds preloaded mesh radius");
    }
    auto generator = std::make_shared<LayeredGenerator>(std::make_shared<Empty>(), catalogue.names,
        std::vector<StructureStamp>{{std::move(structure), {0, 0, 0}}});
    world = std::make_unique<VoxelWorld>(generator, catalogue.palette, settings, jobs);
    for (int attempt = 0; attempt < 1000 && !world->isSettled(); ++attempt) {
        world->update(observer);
        world->finishPendingWork();
    }
    ZODAR_ENSURE(world->isSettled(), "bunker voxel startup failed to settle");
}
}
