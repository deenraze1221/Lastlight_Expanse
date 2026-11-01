#include "Scene/Voxel/Region.h"
#include "Core/Error.h"
#include <doctest/doctest.h>
using namespace zodar::voxel;
TEST_CASE("region: metric layout and negative boundaries") {
    RegionLayout half(0.5f);
    CHECK(half.columns == 4);
    CHECK(half.regionOf({3, 50, 3}) == RegionCoord{0, 0});
    CHECK(half.regionOf({-1, 0, -4}) == RegionCoord{-1, -1});
    CHECK(half.regionOf({-5, 0, 4}) == RegionCoord{-2, 1});
    CHECK(RegionLayout(1).columns == 2);
    CHECK_THROWS_AS(RegionLayout(0.3f), zodar::Error);
    CHECK_THROWS_AS(RegionLayout(0), zodar::Error);
}
TEST_CASE("region: canonical RLE, layers, explicit air and reordered ids") {
    RegionData d; d.baseIdentity = "seed:17/generator:1";
    d.authored[{0,0,0}] = {{0,"stone"}, {1,"stone"}, {2,"stair@2"}};
    d.runtime[{0,0,0}] = {{0,""}};
    const auto bytes = serializeRegion(d);
    CHECK(parseRegion(bytes) == d);
    CHECK(serializeRegion(parseRegion(bytes)) == bytes);
    VoxelChunk chunk;
    applyRegionLayer(d.authored, {0,0,0}, chunk, {"", "stair@2", "stone"});
    applyRegionLayer(d.runtime, {0,0,0}, chunk, {"", "stair@2", "stone"});
    CHECK(chunk.get(0,0,0) == kEmpty);
    CHECK(chunk.get(1,0,0) == 2);
    CHECK(chunk.get(2,0,0) == 1);
    CHECK_THROWS_AS(applyRegionLayer(d.authored, {0,0,0}, chunk, {"", "stone"}), zodar::Error);
    CHECK(chunk.get(0,0,0) == kEmpty); // unknown name rejected before any cell changes
    CHECK_THROWS_AS(parseRegion(bytes + "junk"), zodar::Error);
    CHECK_THROWS_AS(parseRegion(bytes.substr(0, bytes.size()/2)), zodar::Error);
    auto bad = d; bad.runtime[{4,0,0}][0] = "stone";
    CHECK_THROWS_AS(serializeRegion(bad), zodar::Error);
    const auto path = std::filesystem::temp_directory_path() / "zodar-region-test.zregion";
    saveRegion(path, d);
    CHECK(loadRegion(path) == d);
    CHECK_THROWS_AS(saveRegion(path, bad), zodar::Error);
    CHECK(loadRegion(path) == d);
    std::filesystem::remove(path);
}
