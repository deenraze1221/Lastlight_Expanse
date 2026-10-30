#pragma once

#include <volk.h>

#include <cstdint>

namespace raven::gfx {

// Timeline semaphore that counts submissions on one queue. Values increase by one per
// submission; on a single queue they also complete in order.
class GpuTimeline {
public:
    explicit GpuTimeline(VkDevice device);
    ~GpuTimeline();

    GpuTimeline(const GpuTimeline&) = delete;
    GpuTimeline& operator=(const GpuTimeline&) = delete;

    VkSemaphore semaphore() const { return m_semaphore; }
    // Reserves the value the next submission will signal.
    uint64_t reserveNext() { return ++m_lastReserved; }
    uint64_t lastSubmitted() const { return m_lastReserved; }
    uint64_t completedValue() const;
    void wait(uint64_t value) const;

private:
    VkDevice m_device;
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
    uint64_t m_lastReserved = 0;
};

} // namespace raven::gfx
