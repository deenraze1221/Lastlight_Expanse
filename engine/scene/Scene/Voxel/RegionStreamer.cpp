#include "Scene/Voxel/RegionStreamer.h"
#include "Core/JobSystem.h"
#include "Scene/Voxel/Structure.h"
#include "Core/Error.h"
#include <algorithm>
#include <cmath>
namespace zodar::voxel {
RegionStreamer::RegionStreamer(std::shared_ptr<const IVoxelGenerator> base, Settings settings, JobSystem* jobs)
    : m_base(std::move(base)), m_settings(std::move(settings)), m_jobs(jobs), m_layout(m_settings.world.voxelSize) {
    ZODAR_ENSURE(m_base && m_settings.maxRequests > 0 && !m_settings.directory.empty(), "invalid region streamer");
    ZODAR_ENSURE(m_settings.requestBytes >= 4096 && m_settings.maxBytes >= m_settings.requestBytes, "invalid region budget");
    ZODAR_ENSURE(!m_settings.names.empty() && m_settings.names.front().empty(), "region streamer requires stable names");
    ZODAR_ENSURE(m_settings.world.minChunkY <= m_settings.world.maxChunkY && m_settings.world.viewRadius >= 0,
                 "invalid region world bounds");
    std::filesystem::create_directories(m_settings.directory);
}
RegionStreamer::~RegionStreamer() {
    for (auto& [id,r] : m_requests) if (!r.save) *r.cancel = true;
    if (m_jobs) m_jobs->waitIdle();
    integrate();
    // Dirty data must be explicitly flushed by the owner; destructor never starts fallible I/O.
}
std::filesystem::path RegionStreamer::path(RegionCoord c) const {
    return m_settings.directory / (std::to_string(c.x) + "_" + std::to_string(c.z) + ".zregion");
}
void RegionStreamer::submit(RegionCoord coord, bool save) const {
    auto& entry = m_entries[coord];
    const auto id = m_next++;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    const auto snapshot = entry.data;
    const auto revision = entry.revision;
    entry.pending = true;
    m_requests.emplace(id, Request{id, coord, save, revision, cancel, snapshot});
    auto job = [this, id, coord, save, snapshot, revision, cancel] {
        Result result{{id, coord, save, Outcome::Success, {}}, revision, {}};
        try {
            if (*cancel) result.event.outcome = Outcome::Cancelled;
            else if (save) saveRegion(path(coord), *snapshot);
            else {
                RegionData d;
                if (std::filesystem::exists(path(coord))) d = loadRegion(path(coord),m_settings.requestBytes/4);
                else { d.coord=coord; d.voxelSize=m_settings.world.voxelSize; d.minY=m_settings.world.minChunkY;
                    d.maxY=m_settings.world.maxChunkY; d.baseIdentity=m_settings.baseIdentity; }
                ZODAR_ENSURE(d.coord == coord && d.voxelSize == m_settings.world.voxelSize &&
                             d.minY == m_settings.world.minChunkY && d.maxY == m_settings.world.maxChunkY &&
                             d.baseIdentity == m_settings.baseIdentity, "incompatible region baseline");
                // Resolve every name before publishing the region.
                for (const auto* layer : {&d.authored, &d.runtime}) for (const auto& [c,cells] : *layer) {
                    VoxelChunk probe; applyRegionLayer(*layer, c, probe, m_settings.names);
                }
                result.data = std::make_shared<const RegionData>(std::move(d));
                if (*cancel) { result.data.reset(); result.event.outcome=Outcome::Cancelled; }
            }
        } catch (const std::exception& e) { result.event.outcome=Outcome::Failure; result.event.error=e.what(); }
          catch (...) { result.event.outcome=Outcome::Failure; result.event.error="unknown region failure"; }
        std::lock_guard lock(m_mutex); m_results.push_back(std::move(result));
    };
    try { if (m_jobs) m_jobs->submit(std::move(job)); else job(); }
    catch (...) { entry.pending=false; m_requests.erase(id); throw; }
}
void RegionStreamer::integrate() const {
    std::lock_guard lock(m_mutex);
    // A whole logical batch commits together, independent of completion order.
    if (m_results.size() != m_requests.size()) return;
    std::sort(m_results.begin(), m_results.end(), [](const auto& a,const auto& b) { return a.event.request < b.event.request; });
    for (auto& result : m_results) {
        auto& e = m_entries.at(result.event.coord); e.pending=false;
        if (result.event.outcome == Outcome::Success) {
            if (result.event.save) e.saved=result.revision;
            else if (m_wanted.contains(result.event.coord)) { e.data=std::move(result.data); m_resident[result.event.coord]=e.data; m_snapshots.push_back(e.data); }
            else result.event.outcome=Outcome::Cancelled;
        }
        if (result.event.outcome == Outcome::Failure) m_failed.insert(result.event.coord);
        m_events.push_back(std::move(result.event));
    }
    m_results.clear(); m_requests.clear();
}
void RegionStreamer::saveDirty() const {
    for (auto& [coord,e] : m_entries) {
        if (m_requests.size() >= m_settings.maxRequests) break;
        if (memoryBytes()+m_settings.requestBytes > m_settings.maxBytes) { m_pressure=true; break; }
        if (e.data && !e.pending && e.revision != e.saved && !m_failed.contains(coord)) submit(coord,true);
    }
}
void RegionStreamer::updateStreaming(const glm::vec3& observer) const {
    const auto chunkSize = m_settings.world.voxelSize * 32;
    ZODAR_ENSURE(std::isfinite(observer.x) && std::isfinite(observer.z) &&
                 std::abs(observer.x/chunkSize) < 1e8f && std::abs(observer.z/chunkSize) < 1e8f, "invalid region observer");
    const int cx=int(std::floor(observer.x/chunkSize)), cz=int(std::floor(observer.z/chunkSize));
    const int radius=m_settings.world.viewRadius+2;
    std::set<RegionCoord> wanted;
    for (int z=-radius; z<=radius; ++z) for (int x=-radius; x<=radius; ++x)
        wanted.insert(m_layout.regionOf({cx+x,0,cz+z}));
    m_wanted=std::move(wanted);
    for (auto& [id,r] : m_requests) if (!r.save && !m_wanted.contains(r.coord)) *r.cancel=true;
    integrate();
    if (!m_requests.empty()) return;
    saveDirty();
    if (!m_requests.empty()) return;
    for (auto it=m_entries.begin(); it!=m_entries.end();) {
        if (!m_wanted.contains(it->first) && !it->second.pending && it->second.revision == it->second.saved)
            { std::lock_guard lock(m_mutex); m_resident.erase(it->first); it=m_entries.erase(it); }
        else ++it;
    }
    m_pressure=false;
    for (const auto& coord : m_wanted) {
        if (memoryBytes()+m_settings.requestBytes > m_settings.maxBytes) { m_pressure=true; break; }
        if (m_requests.size() >= m_settings.maxRequests) break;
        auto& e=m_entries[coord];
        if (!e.data && !e.pending && !m_failed.contains(coord)) submit(coord,false);
    }
}
bool RegionStreamer::ready(const ChunkCoord& coord) const {
    const auto it=m_entries.find(m_layout.regionOf(coord)); return it!=m_entries.end() && bool(it->second.data);
}
std::shared_ptr<const IVoxelGenerator> RegionStreamer::generatorFor(const ChunkCoord& coord) const {
    struct Snapshot final : IVoxelGenerator {
        std::shared_ptr<const IVoxelGenerator> base;
        std::shared_ptr<const RegionData> data;
        BlockNames names;
        void generate(const ChunkCoord& c,VoxelChunk& out) const override {
            base->generate(c,out); applyRegionLayer(data->authored,c,out,names); applyRegionLayer(data->runtime,c,out,names);
        }
    };
    auto snapshot=std::make_shared<Snapshot>(); snapshot->base=m_base;
    snapshot->data=m_entries.at(m_layout.regionOf(coord)).data; snapshot->names=m_settings.names;
    return snapshot;
}
void RegionStreamer::generate(const ChunkCoord& coord, VoxelChunk& out) const {
    std::shared_ptr<const RegionData> data;
    { std::lock_guard lock(m_mutex); const auto it=m_resident.find(m_layout.regionOf(coord));
      ZODAR_ENSURE(it!=m_resident.end(), "region not ready"); data=it->second; }
    m_base->generate(coord,out);
    applyRegionLayer(data->authored,coord,out,m_settings.names);
    applyRegionLayer(data->runtime,coord,out,m_settings.names);
    out.compact();
}
bool RegionStreamer::recordEdit(const glm::ivec3& voxel, Voxel value) const {
    ZODAR_ENSURE(value < m_settings.names.size(), "edit has no stable block name");
    ZODAR_ENSURE(memoryBytes()+m_settings.requestBytes < m_settings.maxBytes, "region memory pressure; edit rejected before mutation");
    std::lock_guard lock(m_mutex);
    const auto coord=chunkOf(voxel); auto& e=m_entries.at(m_layout.regionOf(coord));
    ZODAR_ENSURE(e.data, "edit region not ready");
    auto copy=std::make_shared<RegionData>(*e.data); const auto l=localOf(voxel);
    copy->runtime[coord][static_cast<uint32_t>(VoxelChunk::index(l.x,l.y,l.z))]=m_settings.names[value];
    ZODAR_ENSURE(regionMemoryBytes(*copy) <= m_settings.requestBytes/4, "region edit capacity exceeded");
    e.data=std::move(copy); m_snapshots.push_back(e.data); m_resident[m_layout.regionOf(coord)]=e.data; ++e.revision; return true;
}
void RegionStreamer::flush() const {
    for (;;) {
        if (m_jobs) m_jobs->waitIdle();
        integrate();
        ZODAR_ENSURE(m_failed.empty(), "region I/O failed; inspect events, dirty data retained");
        saveDirty();
        if(m_requests.empty()) {
            for(const auto& [coord,e]:m_entries) ZODAR_ENSURE(e.revision==e.saved,"region flush blocked by memory pressure; dirty data retained");
            break;
        }
    }
}
std::vector<RegionStreamer::Event> RegionStreamer::takeEvents() { std::vector<Event> out; out.swap(m_events); return out; }
uint64_t RegionStreamer::memoryBytes() const {
    uint64_t bytes=m_requests.size()*m_settings.requestBytes;
    std::set<const RegionData*> counted;
    for(auto it=m_snapshots.begin();it!=m_snapshots.end();) {
        if(auto data=it->lock()) { if(counted.insert(data.get()).second) bytes+=regionMemoryBytes(*data); ++it; }
        else it=m_snapshots.erase(it);
    }
    return bytes;
}
size_t RegionStreamer::residentRegions() const { size_t n=0; for(const auto& [c,e]:m_entries) n+=bool(e.data); return n; }
} // namespace zodar::voxel
