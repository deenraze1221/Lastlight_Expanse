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
    m_entries.emplace(id, Entry{m_tick + delay, repeat, std::move(callback), {}, {}});
    return id;
}
void Timers::registerCallback(std::string name, std::function<void(std::string_view)> callback) {
    if (name.empty() || !callback || m_callbacks.contains(name)) fail("invalid timer callback registration");
    m_callbacks.emplace(std::move(name), std::move(callback));
}
Timers::Id Timers::afterNamed(uint64_t delay, std::string name, std::string payload, uint64_t repeat) {
    const auto callback = m_callbacks.at(name);
    const auto id = after(delay, [callback, payload] { callback(payload); }, repeat);
    m_entries.at(id).name = std::move(name); m_entries.at(id).payload = std::move(payload);
    return id;
}
Timers::Snapshot Timers::capture() const {
    if (m_active) fail("timer snapshot during active tick");
    Snapshot out{m_tick, m_next, {}};
    for (const auto& [id, e] : m_entries) {
        if (e.name.empty()) fail("pending anonymous timer cannot be saved");
        out.entries.push_back({id, e.due, e.repeat, e.name, e.payload});
    }
    return out;
}
Timers Timers::stage(const Snapshot& s) const {
    if (!s.next) fail("invalid next timer ID");
    Timers out; out.m_callbacks = m_callbacks; out.m_tick = s.tick; out.m_next = s.next;
    for (const auto& e : s.entries) {
        if (!e.id || e.id >= s.next || e.due <= s.tick || out.m_entries.contains(e.id)) fail("invalid saved timer");
        const auto callback = m_callbacks.at(e.name);
        out.m_entries.emplace(e.id, Entry{e.due, e.repeat, [callback, payload=e.payload] { callback(payload); }, e.name, e.payload});
    }
    return out;
}
void Timers::commit(Timers&& staged) noexcept {
    m_entries.swap(staged.m_entries); m_tick = staged.m_tick; m_next = staged.m_next;
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
            const auto repeat = it->second.repeat;
            if (repeat && repeat > std::numeric_limits<uint64_t>::max() - it->second.due)
                fail("repeating timer deadline overflow");
            // Move instead of copying: mutable capture state belongs to this timer.
            // Local ownership also keeps the callable alive if it cancels itself.
            auto callback = std::move(it->second.callback);
            if (repeat) it->second.due += repeat;
            else m_entries.erase(it);
            const auto restore = [&] {
                if (const auto pending = m_entries.find(id); pending != m_entries.end())
                    pending->second.callback = std::move(callback);
            };
            try { callback(); }
            catch (...) { restore(); throw; }
            restore();
        }
    } catch (...) { m_active = false; throw; }
    m_active = false;
}
void Timers::advance() { beginTick(); finishTick(); }
} // namespace zodar
