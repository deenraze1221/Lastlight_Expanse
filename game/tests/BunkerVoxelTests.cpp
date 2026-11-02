#include "SceneFile/BlockFile.h"
#include "Assets/ImageData.h"
#include <doctest/doctest.h>
#include <filesystem>

TEST_CASE("bunker voxels: catalogue uses structural blocks and 64 pixel textures") {
    const auto path = std::filesystem::path(LASTLIGHT_ASSETS_DIR) / "blocks/bunker_blocks.blocks.json";
    const auto catalogue = zodar::loadBlockCatalogue(path);
    CHECK(catalogue.uvPerMetre == 1.0f);
    CHECK(catalogue.find("stairs@3").has_value());
    for (size_t i = 1; i < catalogue.palette.blocks.size(); ++i)
        CHECK((catalogue.palette.blocks[i].flags & zodar::voxel::BlockState::kStructural) != 0);
    for (const auto& layer : catalogue.layers) {
        const auto image = zodar::loadImage(layer, zodar::ColorSpace::Srgb);
        CHECK(image.width == 64);
        CHECK(image.height == 64);
    }
}

#include "Bunker/BunkerVoxels.h"
#include "Scene/Voxel/Structure.h"
#include <fstream>
#include <iterator>

TEST_CASE("bunker voxels: structure is canonical and spans negative coordinates") {
    std::ifstream in(std::filesystem::path(LASTLIGHT_ASSETS_DIR) / "structures/bunker.zstruct");
    const std::string bytes{std::istreambuf_iterator<char>(in), {}};
    const auto structure = zodar::voxel::parseStructure(bytes);
    CHECK(zodar::voxel::serializeStructure(structure) == bytes);
    CHECK(structure.size.x > 128); // spans more than one 64 m region
    CHECK(structure.origin.x > 0);
    CHECK(structure.origin.y == 1);
    CHECK(structure.anchors.front().name == "player_start");
    lastlight::BunkerVoxels bunker(LASTLIGHT_ASSETS_DIR);
    auto& world = *bunker.world;
    CHECK(world.isSettled());
    CHECK(world.getVoxel({-22, -1, 0}) == *bunker.catalogue.find("floor"));
    CHECK(world.getVoxel({10, 8, 0}) == *bunker.catalogue.find("ceiling"));
    CHECK(world.getVoxel({10, 2, 0}) == 0);
    CHECK(world.getVoxel({0, -1, 0}) != 0);
}
