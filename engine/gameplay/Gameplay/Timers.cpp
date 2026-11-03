#include "Gameplay/Timers.h"
#include "Core/Error.h"
#include <algorithm>
#include <limits>
#include <vector>

namespace zodar {
Timers::Id Timers::after(uint64_t delay, std::function<void()> callback, uint64_t repeat) {
    if (!delay || !callback) fail("timer requires a positive tick delay and callback");
    if (!m_next || delay > std::numeric_limits<uint64_t>::max() - m_tick) fail("timer ID/deadline overflow");
    const auto id = m_next++;
    m_entries.emplace(id, Entry{m_tick + delay, repeat, std::move(callback)});
    return id;
}
void Timers::beginTick() {
    if (m_active) fail("recursive timer clock advancement");
    if (m_tick == std::numeric_limits<uint64_t>::max()) fail("timer tick overflow");
    ++m_tick;
    m_active = true;
}
void Timers::finishTick() {
    std::vector<std::pair<uint64_t, Id>> due;
    for (const auto& [id, entry] : m_entries) if (entry.due <= m_tick) due.emplace_back(entry.due, id);
    std::sort(due.begin(), due.end());
    try {
        for (const auto& [deadline, id] : due) {
            (void)deadline;
            const auto it = m_entries.find(id);
            if (it == m_entries.end()) continue; // an earlier callback cancelled it
            auto callback = it->second.callback; // safe when callback cancels/reschedules timers
            if (it->second.repeat) {
                if (it->second.repeat > std::numeric_limits<uint64_t>::max() - it->second.due)
                    fail("repeating timer deadline overflow");
                it->second.due += it->second.repeat;
            } else m_entries.erase(it);
            callback();
        }
    } catch (...) { m_active = false; throw; }
    m_active = false;
}
void Timers::advance() { beginTick(); finishTick(); }
} // namespace zodar
