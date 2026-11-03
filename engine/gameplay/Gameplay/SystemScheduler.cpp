#include "Gameplay/SystemScheduler.h"
#include "Core/Error.h"
#include "Core/Profiler.h"
#include <algorithm>
#include <map>

namespace zodar {
namespace {
constexpr size_t kPhases = 5;
size_t index(GameplayPhase phase) {
    const auto value = static_cast<size_t>(phase);
    if (value >= kPhases) fail("invalid gameplay phase");
    return value;
}
}
void SystemScheduler::add(SystemDesc system) {
    if (m_validated) fail("cannot register systems after scheduler validation");
    (void)index(system.phase);
    if (system.name.empty() || !system.run) fail("system needs a name and callback");
    if (std::any_of(m_systems.begin(), m_systems.end(), [&](const auto& e) { return e.system.name == system.name; }))
        fail("duplicate system '" + system.name + "'");
    auto profile = "System/" + system.name;
    m_systems.push_back({std::move(system), std::move(profile)});
}
void SystemScheduler::validate() {
    if (m_validated) return;
    std::map<std::string, size_t> byName;
    for (size_t i = 0; i < m_systems.size(); ++i) byName.emplace(m_systems[i].system.name, i);
    std::vector<std::vector<size_t>> edges(m_systems.size());
    std::vector<size_t> indegree(m_systems.size(), 0);
    const auto edge = [&](size_t from, size_t to) {
        const auto a = index(m_systems[from].system.phase), b = index(m_systems[to].system.phase);
        if (a > b) fail("system dependency reverses gameplay phase order");
        if (a < b) return; // already guaranteed by phase order
        if (std::find(edges[from].begin(), edges[from].end(), to) == edges[from].end()) {
            edges[from].push_back(to);
            ++indegree[to];
        }
    };
    const auto named = [&](const std::string& name) {
        const auto it = byName.find(name);
        if (it == byName.end()) fail("unknown system dependency '" + name + "'");
        return it->second;
    };
    for (size_t i = 0; i < m_systems.size(); ++i) {
        for (const auto& name : m_systems[i].system.before) edge(i, named(name));
        for (const auto& name : m_systems[i].system.after) edge(named(name), i);
    }
    std::vector<std::vector<size_t>> order(kPhases);
    std::vector<bool> done(m_systems.size(), false);
    // ponytail: O(n^2) startup scan keeps ties explicit; use a ready queue only for measured large-graph needs.
    for (size_t phase = 0; phase < kPhases; ++phase) {
        while (true) {
            size_t chosen = m_systems.size();
            bool remaining = false;
            for (size_t i = 0; i < m_systems.size(); ++i) {
                if (done[i] || index(m_systems[i].system.phase) != phase) continue;
                remaining = true;
                if (!indegree[i]) { chosen = i; break; }
            }
            if (!remaining) break;
            if (chosen == m_systems.size()) fail("cycle in gameplay system dependencies");
            done[chosen] = true;
            order[phase].push_back(chosen);
            for (auto next : edges[chosen]) --indegree[next];
        }
    }
    m_order = std::move(order);
    m_validated = true;
}
void SystemScheduler::run(GameplayPhase phase, const SystemContext& context, CpuProfiler* profiler) {
    const auto p = index(phase);
    validate();
    if (m_running) fail("recursive system execution");
    struct Running {
        bool& flag;
        explicit Running(bool& value) : flag(value) { flag = true; }
        ~Running() { flag = false; }
    } running(m_running);
    for (auto i : m_order[p]) {
        const auto& entry = m_systems[i];
        if (profiler) {
            CpuProfiler::Timer scope(*profiler, entry.profileName.c_str());
            entry.system.run(context);
        } else entry.system.run(context);
    }
}
} // namespace zodar
