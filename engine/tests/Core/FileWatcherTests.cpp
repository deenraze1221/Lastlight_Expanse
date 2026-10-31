// Hot reload (C5): polling FileWatcher.
#include "Core/FileWatcher.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace zodar;

namespace {

struct WatchDir {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "zodar_file_watcher_tests";
    WatchDir() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~WatchDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::filesystem::path write(const std::string& name, const std::string& text) const {
        std::ofstream(dir / name, std::ios::binary) << text;
        return dir / name;
    }
    // Moves the modification time forward so same-size edits are detected regardless of the
    // file system's timestamp resolution.
    static void touch(const std::filesystem::path& p) {
        std::filesystem::last_write_time(p, std::filesystem::last_write_time(p) + std::chrono::seconds(2));
    }
};

} // namespace

TEST_CASE("file watcher: reports modified, created and removed files once") {
    const WatchDir d;
    const auto a = d.write("a.txt", "one");
    const auto b = d.dir / "b.txt"; // does not exist yet
    FileWatcher w;
    w.watch(a);
    w.watch(b);
    CHECK(w.size() == 2);
    CHECK(w.poll().empty()); // current state is not a change

    d.write("a.txt", "three"); // size change
    CHECK(w.poll() == std::vector{a});
    CHECK(w.poll().empty()); // reported once

    d.write("a.txt", "fives");
    WatchDir::touch(a); // same size, newer time
    CHECK(w.poll() == std::vector{a});

    d.write("b.txt", "new");
    CHECK(w.poll() == std::vector{b});
    std::filesystem::remove(a);
    CHECK(w.poll() == std::vector{a});
    CHECK(w.poll().empty());

    w.unwatch(b);
    d.write("b.txt", "changed!");
    CHECK(w.poll().empty());
    CHECK_FALSE(w.watching(b));
}

TEST_CASE("file watcher: setWatched keeps recorded state; due() rate-limits") {
    const WatchDir d;
    const auto a = d.write("a.txt", "1");
    const auto b = d.write("b.txt", "2");
    FileWatcher w;
    w.setWatched({a});
    d.write("a.txt", "11");
    w.setWatched({a, b}); // a keeps its old state: the change is still reported
    CHECK(w.poll() == std::vector{a});
    w.setWatched({b});
    CHECK_FALSE(w.watching(a));
    CHECK(w.watching(b));

    CHECK(w.due(std::chrono::hours(1)));
    CHECK_FALSE(w.due(std::chrono::hours(1)));
    CHECK(w.due(std::chrono::seconds(0)));
}

TEST_CASE("file watcher: refresh consumes earlier changes") {
    const WatchDir d;
    const auto a = d.write("a.txt", "1");
    FileWatcher w;
    w.watch(a);
    d.write("a.txt", "22");
    w.watch(a); // already watched: keeps the old state
    w.refresh(a); // records the current state
    CHECK(w.poll().empty());
    const auto b = d.dir / "b.txt";
    w.refresh(b); // also starts watching
    CHECK(w.watching(b));
}
