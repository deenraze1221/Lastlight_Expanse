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
    const BlockNames names={"", "stair", "stair@1", "stair@2", "stair@3"};
    auto r = rotateStructure(s, 1, names);
    CHECK(r.cells.at({0,0,1}) == "stair@1");
    CHECK(r.anchors[0].cell == glm::ivec3(0,0,1));
    CHECK(r.models[0].yaw == 1);
    CHECK(serializeStructure(parseStructure(serializeStructure(s))) == serializeStructure(s));
    CHECK(serializeStructure(rotateStructure(rotateStructure(s, 1,names), 3,names)) == serializeStructure(s));
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
#include "Scene/Voxel/RegionStreamer.h"
#include "Core/JobSystem.h"
TEST_CASE("region: async load, edit during save and persistent air") {
    struct Base : IVoxelGenerator { void generate(const ChunkCoord&, VoxelChunk& out) const override { out=VoxelChunk(1); } };
    const auto root=std::filesystem::temp_directory_path()/"zodar-stream-test";
    std::filesystem::remove_all(root);
    RegionStreamer::Settings settings; settings.directory=root; settings.names={"","stone"};
    settings.world.voxelSize=0.5f; settings.world.minChunkY=0; settings.world.maxChunkY=0; settings.world.viewRadius=0;
    zodar::JobSystem jobs(3);
    {
        RegionStreamer stream(std::make_shared<Base>(),settings,&jobs);
        for(int i=0;i<16 && !stream.ready({0,0,0});++i) { stream.updateStreaming({8,0,8}); jobs.waitIdle(); }
        REQUIRE(stream.ready({0,0,0}));
        CHECK(stream.recordEdit({0,0,0},0));
        stream.updateStreaming({8,0,8}); // submit save of revision 1
        CHECK(stream.recordEdit({1,0,0},0)); // revision 2 must remain dirty
        stream.flush();
        auto events=stream.takeEvents(); REQUIRE_FALSE(events.empty());
        for(const auto& e:events) CHECK(e.outcome==RegionStreamer::Outcome::Success);
        VoxelChunk c; stream.generate({0,0,0},c); CHECK(c.get(0,0,0)==0); CHECK(c.get(1,0,0)==0);
    }
    {
        RegionStreamer stream(std::make_shared<Base>(),settings);
        for(int i=0;i<16 && !stream.ready({0,0,0});++i) stream.updateStreaming({8,0,8});
        REQUIRE(stream.ready({0,0,0})); VoxelChunk c; stream.generate({0,0,0},c);
        CHECK(c.get(0,0,0)==0); CHECK(c.get(1,0,0)==0); CHECK(c.get(2,0,0)==1);
    }
    std::filesystem::remove_all(root);
}
TEST_CASE("region: world failures terminate inline and threaded requests") {
    struct Broken : IVoxelGenerator {
        void generate(const ChunkCoord&, VoxelChunk&) const override { throw std::runtime_error("injected generation failure"); }
    };
    StreamingSettings s; s.viewRadius=0; s.minChunkY=0; s.maxChunkY=0;
    for(unsigned workers : {0u,2u}) {
        zodar::JobSystem jobs(workers);
        VoxelWorld world(std::make_shared<Broken>(),{},s,&jobs);
        if(workers==0) CHECK_THROWS_AS(world.update({0,0,0}),zodar::Error);
        else { world.update({0,0,0}); CHECK_THROWS_AS(world.finishPendingWork(),zodar::Error); }
        CHECK(world.stats().generateInFlight==0);
    }
}
TEST_CASE("region: cancelled loads and incompatible files emit terminal results") {
    struct Base : IVoxelGenerator { void generate(const ChunkCoord&,VoxelChunk&) const override {} };
    const auto root=std::filesystem::temp_directory_path()/"zodar-region-terminal";
    std::filesystem::remove_all(root);
    RegionStreamer::Settings settings; settings.directory=root; settings.names={""}; settings.world.viewRadius=0;
    zodar::JobSystem jobs(2);
    RegionStreamer stream(std::make_shared<Base>(),settings,&jobs);
    stream.updateStreaming({0,0,0});
    stream.updateStreaming({10000,0,10000}); jobs.waitIdle();
    stream.updateStreaming({10000,0,10000});
    const auto events=stream.takeEvents();
    REQUIRE_FALSE(events.empty());
    for(const auto& e:events) CHECK(e.outcome==RegionStreamer::Outcome::Cancelled);
    stream.flush();
    std::filesystem::remove_all(root);
}
TEST_CASE("region: budget pressure blocks admission and oversized decode") {
    struct Base : IVoxelGenerator { void generate(const ChunkCoord&,VoxelChunk& c) const override { c=VoxelChunk(1); } };
    const auto root=std::filesystem::temp_directory_path()/"zodar-region-budget";
    std::filesystem::remove_all(root);
    RegionStreamer::Settings s; s.directory=root; s.names={"","stone"}; s.world.viewRadius=0;
    s.maxBytes=s.requestBytes; // reserve fits, resident data leaves insufficient space for another request
    RegionStreamer stream(std::make_shared<Base>(),s);
    stream.updateStreaming({0,0,0}); stream.updateStreaming({0,0,0});
    CHECK(stream.pressured()); CHECK(stream.memoryBytes()<=s.maxBytes);
    RegionData data; data.runtime[{0,0,0}][0]="stone";
    CHECK_THROWS_AS(parseRegion(serializeRegion(data),1),zodar::Error);
    StreamingSettings worldSettings; worldSettings.viewRadius=0; worldSettings.minChunkY=0; worldSettings.maxChunkY=0;
    worldSettings.cpuBudgetBytes=1;
    VoxelWorld world(std::make_shared<Base>(),{},worldSettings);
    world.update({0,0,0});
    CHECK(world.stats().memoryPressure); CHECK(world.stats().generateInFlight==0);
    std::filesystem::remove_all(root);
}
#include <chrono>
#include <iostream>
TEST_CASE("region: streamed world collision, structural LOD and logical events match worker counts") {
    struct Base : IVoxelGenerator {
        void generate(const ChunkCoord& c,VoxelChunk& out) const override {
            if(c.y==0) out.fillLayer(0,1);
        }
    };
    std::vector<std::tuple<ChunkCoord,int,int>> expected;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned workers:{0u,3u}) {
        const auto root=std::filesystem::temp_directory_path()/("zodar-region-integration-"+std::to_string(workers));
        std::filesystem::remove_all(root);
        zodar::JobSystem jobs(workers);
        RegionStreamer::Settings s; s.directory=root; s.names={"","stone"};
        s.world.voxelSize=0.5f; s.world.viewRadius=1; s.world.minChunkY=0; s.world.maxChunkY=0;
        s.world.maxJobsPerUpdate=8; s.world.maxJobsInFlight=8; s.world.structuralLod0Distance=3;
        s.world.lodDistances={0.01f}; s.world.cpuBudgetBytes=256*1024*1024;
        auto generator=std::make_shared<RegionStreamer>(std::make_shared<Base>(),s,&jobs);
        VoxelPalette palette; palette.colors={glm::vec4(0),glm::vec4(1)};
        palette.shapes=std::make_shared<ShapeLibrary>(ShapeLibrary::builtins()); palette.blocks.resize(2);
        palette.blocks[1]={1,ShapeLibrary::kCube,0,BlockState::kStructural};
        std::vector<std::tuple<ChunkCoord,int,int>> events;
        {
            VoxelWorld world(generator,palette,s.world,&jobs);
            auto settle=[&](glm::vec3 observer) {
                for(int i=0;i<200;++i) {
                    world.update(observer); world.finishPendingWork();
                    for(const auto& e:world.takeMeshEvents()) {
                        events.emplace_back(e.coord,int(e.type),e.lod);
                        if(e.type==ChunkMeshEvent::Type::Upsert) CHECK(e.lod==0);
                    }
                    if(world.isSettled()) return;
                }
                FAIL("region world did not settle");
            };
            settle({8,1,8});
            REQUIRE(world.setVoxel({1,0,1},0)); settle({8,1,8}); generator->flush();
            std::vector<SolidBox> boxes;
            CHECK(world.collectSolidBoxes({{0.6f,0.1f,0.6f},{0.9f,0.4f,0.9f}},boxes)==QueryStatus::Complete);
            CHECK(boxes.empty());
            settle({1024,1,1024}); settle({8,1,8});
            CHECK(world.getVoxel({1,0,1})==kEmpty);
            CHECK(world.stats().editedChunks==0); // persistent provider owns the edits
            const auto captured=captureStructure(world,{0,0,0},{2,1,2},s.names);
            CHECK(captured.cells.at({1,0,1}).empty());
            CHECK(world.stats().cpuBytes<=s.world.cpuBudgetBytes);
            generator->flush();
        }
        if(workers==0) expected=events; else CHECK(events==expected);
        generator.reset(); std::filesystem::remove_all(root);
    }
    std::cout<<"Region integration (inline + 3 workers): "
             <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
}
TEST_CASE("region: failed save retains edits until explicit repaired retry") {
    struct Base : IVoxelGenerator { void generate(const ChunkCoord&,VoxelChunk& c) const override { c=VoxelChunk(1); } };
    const auto root=std::filesystem::temp_directory_path()/"zodar-region-save-failure";
    std::filesystem::remove_all(root);
    RegionStreamer::Settings s; s.directory=root; s.names={"","stone"}; s.world.viewRadius=0;
    s.world.minChunkY=0; s.world.maxChunkY=0;
    RegionStreamer stream(std::make_shared<Base>(),s);
    for(int i=0;i<16 && !stream.ready({0,0,0});++i) stream.updateStreaming({0,0,0});
    REQUIRE(stream.ready({0,0,0})); stream.takeEvents();
    REQUIRE(stream.recordEdit({0,0,0},0));
    const auto blocked=root/"0_0.zregion"; std::filesystem::create_directory(blocked);
    CHECK_THROWS_AS(stream.flush(),zodar::Error);
    auto events=stream.takeEvents(); REQUIRE(events.size()==1);
    CHECK(events[0].save); CHECK(events[0].outcome==RegionStreamer::Outcome::Failure);
    VoxelChunk chunk; stream.generate({0,0,0},chunk); CHECK(chunk.get(0,0,0)==0);
    std::filesystem::remove(blocked); stream.retryFailures(); stream.flush();
    CHECK(loadRegion(blocked).runtime.at({0,0,0}).at(0).empty());
    std::filesystem::remove_all(root);
}
TEST_CASE("region: extreme structure translations reject before integer overflow") {
    Structure s; s.cells[{0,0,0}]=""; s.origin={-1000000000,0,0}; RegionData region;
    CHECK_THROWS_AS(stampStructure(region,s,{2000000000,0,0}),zodar::Error);
    CHECK(region.authored.empty());
}
