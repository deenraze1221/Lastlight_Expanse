#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

// Hot reload support (Phases C5/C6).
//
// File watching is a development feature: it is compiled in only for Debug builds with the
// CMake option ZODAR_HOT_RELOAD (default ON), and every user opts in at runtime
// (AssetManagerDesc::watchFiles, LiveSceneDesc::watch). Explicit reload calls
// (AssetManager::reload*, LiveScene::reload) work in every build.
#if defined(ZODAR_HOT_RELOAD) && ZODAR_HOT_RELOAD
#define ZODAR_HOT_RELOAD_AVAILABLE 1
#else
#define ZODAR_HOT_RELOAD_AVAILABLE 0
#endif

namespace zodar {

// True when automatic file watching is compiled in (Debug build, ZODAR_HOT_RELOAD on).
inline constexpr bool kHotReloadAvailable = ZODAR_HOT_RELOAD_AVAILABLE != 0;

// Polling file watcher: remembers size + modification time of each watched path and reports
// paths whose state changed (modified, created or removed) since the previous poll.
// No OS notification API, no threads: call poll() from the thread that uses the results.
// Paths are compared as given; callers pass absolute, normalised paths.
//
//   FileWatcher w;
//   w.setWatched({"/game/a.png", "/game/b.gltf"}); // records the current state
//   ... file edited ...
//   for (const auto& path : w.poll()) reload(path);
class FileWatcher {
public:
    // Starts watching `path` (state recorded now, so the current content is not a change).
    // No effect if already watched.
    void watch(const std::filesystem::path& path);
    // Watches `path` and records its current state now, also if already watched (the caller
    // is about to read it, so earlier changes are consumed).
    void refresh(const std::filesystem::path& path) { m_files[path] = stat(path); }
    void unwatch(const std::filesystem::path& path);
    // Watches exactly `paths`: new ones are recorded, missing ones dropped, existing ones keep
    // their recorded state (a change made before this call is still reported by poll()).
    void setWatched(const std::vector<std::filesystem::path>& paths);
    void clear() { m_files.clear(); }

    // Changed paths since the last poll (sorted). Updates the recorded states.
    std::vector<std::filesystem::path> poll();

    // Rate limit for callers that poll every frame: true at most once per `interval`
    // (always true for a zero interval). The first call returns true.
    bool due(std::chrono::steady_clock::duration interval);

    bool watching(const std::filesystem::path& path) const { return m_files.contains(path); }
    size_t size() const { return m_files.size(); }

private:
    struct State {
        bool exists = false;
        uintmax_t size = 0;
        std::filesystem::file_time_type modified{};
        bool operator==(const State&) const = default;
    };
    static State stat(const std::filesystem::path& path);

    std::map<std::filesystem::path, State> m_files;
    std::optional<std::chrono::steady_clock::time_point> m_lastPoll;
};

} // namespace zodar
