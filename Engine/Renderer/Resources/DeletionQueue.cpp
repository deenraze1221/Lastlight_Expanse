#include "Renderer/Resources/DeletionQueue.h"

#include <utility>

namespace zodar::gfx {

DeletionQueue::~DeletionQueue() {
    flushAll();
}

void DeletionQueue::retire(std::function<void()> destroy) {
    m_pending.push_back(std::move(destroy));
}

void DeletionQueue::onSubmit(uint64_t timelineValue, bool frameSubmission, bool frameRecording) {
    // A non-frame submission (e.g. an upload) issued in the middle of frame recording must
    // not adopt pending items: the frame's command buffer may still reference them.
    if (!frameSubmission && frameRecording) {
        return;
    }
    for (auto& destroy : m_pending) {
        m_scheduled.push_back({timelineValue, std::move(destroy)});
    }
    m_pending.clear();
}

size_t DeletionQueue::collect(uint64_t completedValue) {
    size_t done = 0;
    while (done < m_scheduled.size() && m_scheduled[done].value <= completedValue) {
        m_scheduled[done].destroy();
        ++done;
    }
    m_scheduled.erase(m_scheduled.begin(), m_scheduled.begin() + static_cast<std::ptrdiff_t>(done));
    return done;
}

void DeletionQueue::flushAll() {
    for (auto& item : m_scheduled) {
        item.destroy();
    }
    m_scheduled.clear();
    for (auto& destroy : m_pending) {
        destroy();
    }
    m_pending.clear();
}

} // namespace zodar::gfx
