#include "RegionScenario.h"
#include "Assets/Primitives.h"
#include "Core/Error.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <ostream>

using namespace zodar;
using namespace zodar::voxel;
using namespace zodar::gfx;
namespace {
struct Terrain final : IVoxelGenerator {
    void generate(const ChunkCoord& c, VoxelChunk& out) const override {
        out = VoxelChunk();
        if (c.y != 0) return;
        for (int y=0; y<3; ++y) out.fillLayer(y, 1);
        // Repeatable structural walls, gaps and slabs in every loaded chunk.
        for (int z=8; z<24; ++z) for (int y=3; y<9; ++y) {
            out.set(8,y,z,2);
            if (z != 16) out.set(23,y,z,2);
        }
        for (int x=9; x<23; ++x) for (int z=8; z<24; ++z) out.set(x,9,z,3);
    }
};
double elapsed(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
}
void distribution(std::ostream& out, std::vector<double> values) {
    std::sort(values.begin(),values.end());
    double sum=0; for (double v:values) sum+=v;
    const auto percentile=[&](double p) { return values.empty()?0.0:values[size_t(p*double(values.size()-1))]; };
    out << "{\"samples\":" << values.size() << ",\"avg\":" << (values.empty()?0:sum/double(values.size()))
        << ",\"p50\":" << percentile(.5) << ",\"p95\":" << percentile(.95) << ",\"p99\":" << percentile(.99) << '}';
}
}
RegionScenario::RegionScenario(VulkanContext& context, UploadContext& upload, MaterialHandle mat,
                               const std::filesystem::path& directory) : meshes(context,upload), material(mat) {
    ZODAR_ENSURE(!std::filesystem::exists(directory) || std::filesystem::is_empty(directory),
                 "region benchmark requires a fresh output region directory");
    StreamingSettings settings;
    settings.voxelSize=1; settings.viewRadius=2; settings.minChunkY=0; settings.maxChunkY=0;
    settings.maxJobsPerUpdate=16; settings.maxJobsInFlight=16;
    settings.cpuBudgetBytes=256*1024*1024; settings.lodDistances={2,4};
    settings.structuralLod0Distance=3;
    VoxelPalette palette;
    palette.colors={{0,0,0,0},{.3f,.4f,.3f,1},{.6f,.6f,.65f,1}};
    palette.shapes=std::make_shared<ShapeLibrary>(ShapeLibrary::builtins());
    palette.blocks={{},{1,0,0,0},{2,0,0,BlockState::kStructural},
                    {2,*palette.shapes->find("slab_low"),0,BlockState::kStructural}};
    RegionStreamer::Settings io;
    io.directory=directory; io.world=settings; io.names={"","ground","wall","slab"};
    io.baseIdentity="zodar-region-bench-v1";
    regions=std::make_shared<RegionStreamer>(std::make_shared<Terrain>(),io);
    world=std::make_unique<VoxelWorld>(regions,std::move(palette),settings);
}
void RegionScenario::update(uint32_t tick, SceneView& view, std::vector<MeshDraw>& draws) {
    // 16 stops cover the complete 4 x 4 km envelope, then return to persisted edits.
    // Hold each stop 32 updates so generation, meshing and uploads can settle.
    const uint32_t stop=(tick/32)%17;
    const uint32_t i=stop==16?0:stop;
    const float x=float(i%4)*(4000.0f/3), z=float(i/4)*(4000.0f/3);
    view.camera.position={x+16,18,z+28};
    view.camera.lookAt({x+16,4,z+12});
    view.camera.projection.nearPlane=.1f;
    const glm::vec3 observer(x,8,z);
    auto start=std::chrono::steady_clock::now();
    world->update(observer);
    if (stop==16 && tick%32==20) {
        ZODAR_ENSURE(world->hasChunkData({0,0,0}) && world->getVoxel({8,4,12})==0,
                     "region benchmark edit did not survive reload");
        reloadVerified=true;
    }
    if (tick%32==24) {
        const glm::ivec3 cell((int(x)/32)*32+8,4,(int(z)/32)*32+12);
        if (world->setVoxel(cell,0)) ++edits;
        regions->flush();
    }
    updateMs.push_back(elapsed(start));
    for (const auto& event:regions->takeEvents()) {
        ZODAR_ENSURE(event.outcome!=RegionStreamer::Outcome::Failure,event.error);
        if(event.outcome!=RegionStreamer::Outcome::Success) continue;
        if(event.save) ++saves;
        else { ++loads; if(visited.contains(event.coord)) ++reloads; visited[event.coord]=true; }
    }
    start=std::chrono::steady_clock::now();
    for (auto& event:world->takeMeshEvents()) {
        if(event.type==ChunkMeshEvent::Type::Remove) {
            const auto it=keys.find(event.coord);
            if(it!=keys.end()) { meshes.remove(it->second); keys.erase(it); }
        } else {
            auto [it,inserted]=keys.try_emplace(event.coord,nextKey);
            if(inserted) ++nextKey;
            ZODAR_ENSURE(meshes.tryUpsert(it->second,event.mesh,glm::translate(glm::mat4(1),event.origin),
                                        material,128*1024*1024),"region benchmark GPU budget exhausted");
        }
    }
    meshes.update();
    meshes.appendDraws(draws);
    uploadMs.push_back(elapsed(start));
    const auto cpu=world->stats(); const auto gpu=meshes.stats();
    peakCpu=std::max(peakCpu,cpu.cpuBytes+regions->memoryBytes());
    peakGpu=std::max(peakGpu,gpu.gpuBytes+gpu.stagingBytes);
    peakChunks=std::max(peakChunks,uint64_t(cpu.chunksWithData));
    peakRegions=std::max(peakRegions,uint64_t(regions->residentRegions()));
    unsettled+=!world->isSettled();
}
void RegionScenario::flush() { regions->flush(); }
void RegionScenario::writeReport(std::ostream& out) const {
    const auto gpu=meshes.stats(); const auto cpu=world->stats();
    out << "  \"region\": {\"extentMeters\":[4000,4000],\"stopFrames\":32,\"stops\":17,"
        << "\"inlineJobs\":true,\"generated\":" << cpu.generated << ",\"meshed\":" << cpu.meshed
        << ",\"generationMs\":" << cpu.generationMs << ",\"meshingMs\":" << cpu.meshingMs
        << ",\"edits\":" << edits << ",\"loads\":" << loads << ",\"saves\":" << saves << ",\"reloads\":" << reloads
        << ",\"peakCpuBytes\":" << peakCpu << ",\"peakGpuAndStagingBytes\":" << peakGpu
        << ",\"uploadedBytes\":" << gpu.uploadedBytes << ",\"peakChunks\":" << peakChunks
        << ",\"peakRegions\":" << peakRegions << ",\"unsettledUpdates\":" << unsettled
        << ",\"reloadVerified\":" << (reloadVerified?"true":"false")
        << ",\"finalSettled\":" << (world->isSettled()?"true":"false")
        << ",\"finalBacklog\":" << cpu.generateInFlight+cpu.meshInFlight+gpu.pendingUploads
        << ",\"updateIncludingMeshingAndIoMs\":";
    distribution(out,updateMs); out << ",\"uploadSubmitMs\":"; distribution(out,uploadMs); out << "},\n";
}
