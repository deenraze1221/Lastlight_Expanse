#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace raven::gfx {

// Defers destruction of GPU objects until the GPU timeline has passed every submission
// that may reference them.
//
// Contract for retire(): the object may still be referenced by already submitted work or by
// the command buffer currently being recorded for a frame, but by nothing recorded later.
// Pending items are therefore bound to the next *frame* submission (or to any submission
// made while no frame is being recorded) and run once that timeline value completes.
// Pure bookkeeping; no Vulkan calls (unit tested).
class DeletionQueue {
public:
    DeletionQueue() = default;
    ~DeletionQueue();

    DeletionQueue(const DeletionQueue&) = delete;
    DeletionQueue& operator=(const DeletionQueue&) = delete;

    void retire(std::function<void()> destroy);

    // Called for every queue submission that signals `timelineValue`.
    void onSubmit(uint64_t timelineValue, bool frameSubmission, bool frameRecording);

    // Runs every item whose submission value is <= completedValue. Returns how many ran.
    size_t collect(uint64_t completedValue);

    // Runs everything. Only valid when the device is idle.
    void flushAll();

    size_t pendingCount() const { return m_pending.size(); }
    size_t scheduledCount() const { return m_scheduled.size(); }

private:
    struct Item {
        uint64_t value;
        std::function<void()> destroy;
    };
    std::vector<std::function<void()>> m_pending;
    std::vector<Item> m_scheduled; // Sorted by value (submissions are monotonic).
};

} // namespace raven::gfx
