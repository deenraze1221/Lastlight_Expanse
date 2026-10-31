#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace zodar::test {

// Reads a whole file. The stream lives only inside this function, so no handle stays
// open afterwards (Windows refuses to delete files that are still open).
inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Owns a file in the temp directory: removes any stale copy on creation and deletes the
// file when it goes out of scope. remove() reports failure so tests can check that every
// handle to the file was closed.
class TempFile {
public:
    explicit TempFile(const std::string& name) : m_path(std::filesystem::temp_directory_path() / name) {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const std::filesystem::path& path() const { return m_path; }

    // True if the file no longer exists afterwards (removed now or already gone).
    bool remove() const {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
        return !ec && !std::filesystem::exists(m_path);
    }

    void write(const std::string& text) const {
        std::ofstream file(m_path, std::ios::binary | std::ios::trunc);
        file << text;
    }

private:
    std::filesystem::path m_path;
};

} // namespace zodar::test
