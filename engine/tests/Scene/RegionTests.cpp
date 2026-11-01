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
#include "Scene/Voxel/Structure.h"
TEST_CASE("region: structures rotate, cross boundaries and compose layers") {
    Structure s; s.size = {2,1,1}; s.cells[{0,0,0}] = "stair"; s.cells[{1,0,0}] = "";
    s.anchors.push_back({"entry", {0,0,0}}); s.models.push_back({"crate", {0.5f,0,0.5f}, 0});
    auto r = rotateStructure(s, 1);
    CHECK(r.cells.at({0,0,1}) == "stair@1");
    CHECK(r.anchors[0].cell == glm::ivec3(0,0,1));
    CHECK(r.models[0].yaw == 1);
    CHECK(serializeStructure(parseStructure(serializeStructure(s))) == serializeStructure(s));
    CHECK(serializeStructure(rotateStructure(rotateStructure(s, 1), 3)) == serializeStructure(s));
    RegionData left, right; right.coord = {1,0};
    stampStructure(left, s, {127,0,0}); stampStructure(right,s,{127,0,0});
    CHECK(left.authored.at({3,0,0}).at(31) == "stair");
    CHECK(right.authored.at({4,0,0}).at(0) == "");
    struct Base : IVoxelGenerator { void generate(const ChunkCoord&, VoxelChunk& out) const override { out = VoxelChunk(1); } };
    RegionLayer authored, runtime; authored[{0,0,0}][0] = "stone"; runtime[{0,0,0}][0] = "";
    LayeredGenerator gen(std::make_shared<Base>(), {"", "stone", "stair"}, {{s,{0,0,0}}}, authored, runtime);
    VoxelChunk chunk; gen.generate({0,0,0}, chunk);
    CHECK(chunk.get(0,0,0) == 0); CHECK(chunk.get(1,0,0) == 0); CHECK(chunk.get(2,0,0) == 1);
    CHECK_THROWS_AS(parseStructure(serializeStructure(s)+"bad"), zodar::Error);
}
