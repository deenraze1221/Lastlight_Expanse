#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace zodar {

// Fixed-size worker thread pool for CPU work that does not touch Vulkan (terrain
// generation, meshing, decoding). Jobs are executed in submission order by whichever worker
// is free; completion order is therefore not deterministic. Callers that need deterministic
// results order them themselves (see voxel::VoxelWorld).
//
// With zero workers, submit() runs the job inline on the calling thread, which makes
// single-threaded, fully deterministic runs (tests, benchmarks) possible with the same code.
class JobSystem {
public:
    using Job = std::function<void()>;

    // workerCount: 0 = run inline; kAutomatic = hardware threads - 1 (at least 1).
    static constexpr uint32_t kAutomatic = UINT32_MAX;
    explicit JobSystem(uint32_t workerCount = kAutomatic);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(Job job);
    // Blocks until every submitted job has finished.
    void waitIdle();

    uint32_t workerCount() const { return static_cast<uint32_t>(m_workers.size()); }
    // Jobs submitted but not finished yet (queued + running).
    uint32_t pendingJobs() const;

private:
    void workerLoop(std::stop_token stop);

    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::condition_variable m_idle;
    std::deque<Job> m_queue;
    uint32_t m_unfinished = 0;
    std::vector<std::jthread> m_workers;
};

} // namespace zodar
