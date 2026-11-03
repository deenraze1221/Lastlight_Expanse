#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>

namespace zodar {
class Gameplay;
// Main-thread fixed-tick timers. IDs are monotonic; callbacks with equal deadlines use ID order.
class Timers {
public:
    using Id = uint64_t;
    Id after(uint64_t delay, std::function<void()> callback, uint64_t repeat = 0);
    bool cancel(Id id) { return m_entries.erase(id) != 0; }
    size_t pending() const { return m_entries.size(); }
    uint64_t tick() const { return m_tick; }
    // Standalone clock for CPU tests/tools. Application owns this clock during gameplay.
    void advance();
private:
    friend class Gameplay;
    struct Entry { uint64_t due, repeat; std::function<void()> callback; };
    void beginTick();
    void finishTick();
    std::map<Id, Entry> m_entries;
    Id m_next = 1;
    uint64_t m_tick = 0;
    bool m_active = false;
};
} // namespace zodar
