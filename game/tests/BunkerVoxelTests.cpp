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

#include "Bunker/BunkerVoxels.h"
#include "Physics/CollisionWorld.h"

TEST_CASE("bunker voxels: hybrid collision blocks walls and missing chunks") {
    lastlight::BunkerVoxels bunker(LASTLIGHT_ASSETS_DIR);
    lastlight::CollisionWorld collision;
    collision.setVoxelWorld(bunker.world.get());
    const lastlight::Aabb body{{-11.2f, 0.01f, -0.2f}, {-10.8f, 1.81f, 0.2f}};
    CHECK_FALSE(collision.overlaps(body));
    const auto ground = collision.sweep(body, {0, -2, 0});
    CHECK(ground.hitY);
    CHECK(ground.moved.y > -0.02f);
    collision.addStatic({{-10, 0, -0.5f}, {-9.5f, 2, 0.5f}});
    const auto wall = collision.sweep(body, {20, 0, 0});
    CHECK(wall.hitX);
    CHECK(wall.moved.x < 1);
    const auto model = collision.raycast({-11, 1, 0}, {1, 0, 0}, 10);
    REQUIRE(model);
    CHECK(model->distance == doctest::Approx(1));
    const auto floor = collision.raycast({-11, 1, 0}, {0, -1, 0}, 10);
    REQUIRE(floor);
    CHECK(floor->distance == doctest::Approx(1));
    CHECK(collision.overlaps({{1000, 0, 0}, {1001, 2, 1}}));
    CHECK(collision.sweep(body, {1000, 0, 0}).moved.x == 0);
    CHECK_THROWS(collision.raycast({-11, 1, 0}, {1, 0, 0}, 1000));
}

#include "Core/JobSystem.h"

TEST_CASE("bunker voxels: threaded startup preserves authored content and produces meshes") {
    lastlight::BunkerVoxels inlineWorld(LASTLIGHT_ASSETS_DIR);
    zodar::JobSystem jobs(3);
    lastlight::BunkerVoxels threadedWorld(LASTLIGHT_ASSETS_DIR, &jobs);
    CHECK_FALSE(inlineWorld.world->takeMeshEvents().empty());
    CHECK_FALSE(threadedWorld.world->takeMeshEvents().empty());
    bool equal = true;
    for (int y = -1; y <= 8; ++y)
        for (int z = -28; z <= 28; ++z)
            for (int x = -34; x <= 122; ++x)
                equal &= inlineWorld.world->getVoxel({x,y,z}) == threadedWorld.world->getVoxel({x,y,z});
    CHECK(equal);
}
