#pragma once
#include "Scene/Voxel/Region.h"
#include <atomic>
#include <set>
namespace zodar::voxel {
// Main-thread control, immutable worker inputs. Owns one region directory (single writer).
class RegionStreamer final : public IVoxelGenerator {
public:
    struct Settings {
        std::filesystem::path directory;
        StreamingSettings world;
        BlockNames names;
        std::string baseIdentity;
        uint32_t maxRequests = 4;
        uint64_t requestBytes = 32 * 1024 * 1024; // reservation includes decode/encode scratch
        uint64_t maxBytes = 256 * 1024 * 1024;
    };
    enum class Outcome { Success, Cancelled, Failure };
    struct Event { uint64_t request; RegionCoord coord; bool save; Outcome outcome; std::string error; };
    RegionStreamer(std::shared_ptr<const IVoxelGenerator> base, Settings settings, JobSystem* jobs = nullptr);
    ~RegionStreamer() override;
    std::shared_ptr<const IVoxelGenerator> generatorFor(const ChunkCoord& coord) const override;
    bool deterministicStreaming() const override { return true; }
    void updateStreaming(const glm::vec3& observer) const override;
    bool ready(const ChunkCoord& coord) const override;
    bool recordEdit(const glm::ivec3& voxel, Voxel value) const override;
    void generate(const ChunkCoord& coord, VoxelChunk& out) const override;
    // Flush schedules dirty saves and waits. Throws on failure; dirty state stays retained.
    void flush() const;
    // Explicit retry only after repairing the reported I/O/configuration failure.
    void retryFailures() { m_failed.clear(); }
    std::vector<Event> takeEvents();
    uint64_t memoryBytes() const;
    size_t residentRegions() const;
    bool pressured() const { return m_pressure; }
private:
    struct Entry { std::shared_ptr<const RegionData> data; uint64_t revision=0, saved=0; bool pending=false; };
    struct Result { Event event; uint64_t revision=0; std::shared_ptr<const RegionData> data; };
    struct Request { uint64_t id; RegionCoord coord; bool save; uint64_t revision; std::shared_ptr<std::atomic_bool> cancel; std::shared_ptr<const RegionData> snapshot; };
    void integrate() const;
    void submit(RegionCoord coord, bool save) const;
    void saveDirty() const;
    std::filesystem::path path(RegionCoord coord) const;
    std::shared_ptr<const IVoxelGenerator> m_base;
    Settings m_settings;
    std::shared_ptr<const BlockNames> m_names;
    JobSystem* m_jobs;
    RegionLayout m_layout;
    mutable std::mutex m_mutex;
    mutable std::vector<std::weak_ptr<const RegionData>> m_snapshots;
    mutable std::map<RegionCoord, Entry> m_entries;
    mutable std::map<RegionCoord, std::shared_ptr<const RegionData>> m_resident;
    mutable std::map<uint64_t, Request> m_requests;
    mutable std::vector<Result> m_results;
    mutable std::vector<Event> m_events;
    mutable std::set<RegionCoord> m_wanted;
    mutable std::set<RegionCoord> m_failed;
    mutable uint64_t m_next=1;
    mutable bool m_pressure=false;
};
} // namespace zodar::voxel
