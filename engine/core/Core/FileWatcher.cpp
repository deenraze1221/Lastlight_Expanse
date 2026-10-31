#include "Core/FileWatcher.h"

#include <set>
#include <system_error>

namespace zodar {

FileWatcher::State FileWatcher::stat(const std::filesystem::path& path) {
    State s;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return s;
    }
    s.size = std::filesystem::file_size(path, ec);
    if (ec) {
        return {};
    }
    s.modified = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return {};
    }
    s.exists = true;
    return s;
}

void FileWatcher::watch(const std::filesystem::path& path) {
    if (!m_files.contains(path)) {
        m_files.emplace(path, stat(path));
    }
}

void FileWatcher::unwatch(const std::filesystem::path& path) { m_files.erase(path); }

void FileWatcher::setWatched(const std::vector<std::filesystem::path>& paths) {
    const std::set<std::filesystem::path> wanted(paths.begin(), paths.end());
    std::erase_if(m_files, [&](const auto& entry) { return !wanted.contains(entry.first); });
    for (const std::filesystem::path& p : wanted) {
        watch(p);
    }
}

std::vector<std::filesystem::path> FileWatcher::poll() {
    std::vector<std::filesystem::path> changed;
    for (auto& [path, state] : m_files) {
        const State now = stat(path);
        if (!(now == state)) {
            state = now;
            changed.push_back(path);
        }
    }
    return changed; // std::map order: sorted
}

bool FileWatcher::due(std::chrono::steady_clock::duration interval) {
    const auto now = std::chrono::steady_clock::now();
    if (m_lastPoll && now - *m_lastPoll < interval) {
        return false;
    }
    m_lastPoll = now;
    return true;
}

} // namespace zodar
