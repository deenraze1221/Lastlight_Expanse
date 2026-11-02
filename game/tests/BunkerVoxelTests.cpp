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
