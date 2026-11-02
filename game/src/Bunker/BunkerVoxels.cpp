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
    auto generator = std::make_shared<LayeredGenerator>(std::make_shared<Empty>(), catalogue.names,
        std::vector<StructureStamp>{{std::move(structure), {0, 0, 0}}});
    StreamingSettings settings;
    settings.voxelSize = 0.5f;
    settings.viewRadius = 4;
    settings.minChunkY = -1;
    settings.maxChunkY = 0;
    settings.lodDistances = {1000.0f};
    settings.mesh.uvPerMetre = catalogue.uvPerMetre;
    world = std::make_unique<VoxelWorld>(generator, catalogue.palette, settings, jobs);
    for (int attempt = 0; attempt < 1000 && !world->isSettled(); ++attempt) {
        world->update({24, 1, 0});
        world->finishPendingWork();
    }
    ZODAR_ENSURE(world->isSettled(), "bunker voxel startup failed to settle");
}
}
