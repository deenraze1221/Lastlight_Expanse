#include "Core/JobSystem.h"

#include "Core/Log.h"

#include <algorithm>
#include <exception>

namespace zodar {

JobSystem::JobSystem(uint32_t workerCount) {
    if (workerCount == kAutomatic) {
        const uint32_t hw = std::thread::hardware_concurrency();
        workerCount = std::max(1u, hw > 1 ? hw - 1 : 1u);
    }
    m_workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this](std::stop_token stop) { workerLoop(stop); });
    }
}

JobSystem::~JobSystem() {
    waitIdle();
    for (auto& worker : m_workers) {
        worker.request_stop();
    }
    m_wake.notify_all();
    m_workers.clear(); // joins
}

void JobSystem::submit(Job job) {
    if (m_workers.empty()) {
        job();
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(std::move(job));
        ++m_unfinished;
    }
    m_wake.notify_one();
}

void JobSystem::waitIdle() {
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_unfinished == 0; });
}

uint32_t JobSystem::pendingJobs() const {
    std::lock_guard lock(m_mutex);
    return m_unfinished;
}

void JobSystem::workerLoop(std::stop_token stop) {
    while (true) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return !m_queue.empty(); })) {
                return; // stop requested and nothing left to do
            }
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }
        try {
            job();
        } catch (const std::exception& e) {
            // Jobs report their own errors; an escaping exception must not kill the worker.
            ZODAR_LOG_ERROR("Unhandled exception in job: {}", e.what());
        }
        {
            std::lock_guard lock(m_mutex);
            --m_unfinished;
            if (m_unfinished == 0) {
                m_idle.notify_all();
            }
        }
    }
}

} // namespace zodar
