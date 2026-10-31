#pragma once

// Internal to Zodar::SceneFile (not part of the public API): nlohmann/json alias and
// validating field readers shared by MaterialFile.cpp and SceneFile.cpp.
#include "SceneFile/DataError.h"

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zodar {
struct MaterialData;
}

namespace zodar::detail {

// Ordered keys (written in insertion order) and float numbers, so serialized files are
// readable, deterministic and round-trip floats exactly with the shortest representation.
using Json = nlohmann::basic_json<nlohmann::ordered_map, std::vector, std::string, bool, std::int64_t,
                                  std::uint64_t, float>;

// Maps JSON pointers ("/entities/2/transform") to the 1-based line:column where that value
// starts in the source text. Built by a small scanner over text nlohmann already accepted.
class JsonLocator {
public:
    struct Position {
        uint32_t line = 0;
        uint32_t column = 0;
    };

    JsonLocator() = default;
    explicit JsonLocator(std::string_view text) : m_text(text) {
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                m_lineStarts.push_back(i + 1);
            }
        }
        std::size_t i = 0;
        skipSpace(i);
        value(i, "", 0);
    }

    // Position of `path`, or of its nearest recorded ancestor (e.g. the object a missing
    // field belongs to); {0, 0} if nothing is known.
    [[nodiscard]] Position find(std::string path) const {
        for (;;) {
            if (const auto it = m_positions.find(path); it != m_positions.end()) {
                return position(it->second);
            }
            if (path.empty()) {
                return {};
            }
            path.resize(path.rfind('/') == std::string::npos ? 0 : path.rfind('/'));
        }
    }

private:
    static constexpr int kMaxDepth = 512;

    Position position(std::size_t offset) const {
        const auto it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), offset);
        const std::size_t line = static_cast<std::size_t>(it - m_lineStarts.begin()); // 1-based (index 0 = line 1).
        return {static_cast<uint32_t>(line), static_cast<uint32_t>(offset - m_lineStarts[line - 1] + 1)};
    }
    void skipSpace(std::size_t& i) const {
        while (i < m_text.size() && (m_text[i] == ' ' || m_text[i] == '\t' || m_text[i] == '\n' || m_text[i] == '\r')) {
            ++i;
        }
    }
    std::string string(std::size_t& i) const { // At '"'; returns the (simply) unescaped text.
        std::string out;
        for (++i; i < m_text.size() && m_text[i] != '"'; ++i) {
            if (m_text[i] == '\\' && i + 1 < m_text.size()) {
                ++i;
            }
            out += m_text[i];
        }
        ++i;
        return out;
    }
    void value(std::size_t& i, const std::string& path, int depth) {
        if (i >= m_text.size() || depth > kMaxDepth) {
            return;
        }
        m_positions.emplace(path, i);
        const char c = m_text[i];
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++i;
            skipSpace(i);
            for (std::size_t index = 0; i < m_text.size() && m_text[i] != close; ++index) {
                std::string key = std::to_string(index);
                if (c == '{') {
                    key = string(i);
                    skipSpace(i);
                    ++i; // ':'
                    skipSpace(i);
                }
                value(i, path + "/" + key, depth + 1);
                skipSpace(i);
                if (i < m_text.size() && m_text[i] == ',') {
                    ++i;
                    skipSpace(i);
                }
            }
            ++i;
        } else if (c == '"') {
            (void)string(i);
        } else {
            while (i < m_text.size() && m_text[i] != ',' && m_text[i] != '}' && m_text[i] != ']' && m_text[i] != ' ' &&
                   m_text[i] != '\n' && m_text[i] != '\r' && m_text[i] != '\t') {
                ++i;
            }
        }
    }

    std::string_view m_text;
    std::vector<std::size_t> m_lineStarts{0};
    std::unordered_map<std::string, std::size_t> m_positions;
};

class Issues {
public:
    explicit Issues(std::string file, const JsonLocator* locator = nullptr)
        : m_file(std::move(file)), m_locator(locator) {}

    void add(std::string path, std::string message) {
        const JsonLocator::Position at = m_locator ? m_locator->find(path) : JsonLocator::Position{};
        m_list.push_back({m_file, at.line, at.column, std::move(path), std::move(message)});
    }
    void append(const std::vector<DataIssue>& issues) { m_list.insert(m_list.end(), issues.begin(), issues.end()); }
    [[nodiscard]] bool empty() const noexcept { return m_list.empty(); }
    [[nodiscard]] std::size_t count() const noexcept { return m_list.size(); }
    [[nodiscard]] const std::string& file() const noexcept { return m_file; }
    void throwIfAny() const {
        if (!m_list.empty()) {
            throw DataError(m_list);
        }
    }

private:
    std::string m_file;
    const JsonLocator* m_locator = nullptr;
    std::vector<DataIssue> m_list;
};

inline std::string typeName(const Json& value) {
    if (value.is_number()) {
        return "number";
    }
    return value.type_name();
}

inline Json parseJson(std::string_view text, const std::string& file) {
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        // Convert the byte offset (1-based, points just past the failing token) to line:column.
        uint32_t line = 1;
        uint32_t column = 1;
        const std::size_t end = std::min<std::size_t>(e.byte > 0 ? e.byte - 1 : 0, text.size());
        for (std::size_t i = 0; i < end; ++i) {
            if (text[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        std::string message = e.what();
        if (const auto pos = message.find("] "); pos != std::string::npos) {
            message = message.substr(pos + 2); // Drop "[json.exception.parse_error.101] ".
        }
        throw DataError({{file, line, column, "", "invalid JSON: " + message}});
    }
}

inline std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw DataError({{path.generic_string(), 0, 0, "", "cannot open file"}});
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

inline std::string child(const std::string& path, std::string_view key) {
    return path + "/" + std::string(key);
}
inline std::string child(const std::string& path, std::size_t index) {
    return path + "/" + std::to_string(index);
}

// Reports every key of `object` that is not in `allowed`.
template <typename Names>
void checkKeysIn(const Json& object, const std::string& path, const Names& allowed, Issues& issues) {
    for (const auto& [key, value] : object.items()) {
        bool known = false;
        for (std::string_view name : allowed) {
            known = known || key == name;
        }
        if (!known) {
            std::string message = "unknown field '" + key + "'";
            if (key.ends_with("Texture") || key == "textures") {
                message += " (textures in material files are not supported yet; planned with the Phase C asset manager)";
            }
            issues.add(child(path, key), message);
        }
    }
}

inline void checkKeys(const Json& object, const std::string& path, std::initializer_list<std::string_view> allowed,
                      Issues& issues) {
    checkKeysIn(object, path, allowed, issues);
}

inline bool requireObject(const Json& value, const std::string& path, Issues& issues) {
    if (!value.is_object()) {
        issues.add(path, "expected an object, got " + typeName(value));
        return false;
    }
    return true;
}

// Field readers: leave `out` unchanged and return false if the key is missing (no issue) or
// invalid (issue reported).
inline bool readFloatValue(const Json& value, const std::string& path, float& out, Issues& issues,
                           float min = -std::numeric_limits<float>::max(),
                           float max = std::numeric_limits<float>::max()) {
    if (!value.is_number()) {
        issues.add(path, "expected a number, got " + typeName(value));
        return false;
    }
    const float v = value.get<float>();
    if (!std::isfinite(v)) {
        issues.add(path, "number is not finite");
        return false;
    }
    if (v < min || v > max) {
        std::ostringstream message;
        message << "value " << v << " is outside [" << min << ", " << max << "]";
        issues.add(path, message.str());
        return false;
    }
    out = v;
    return true;
}

inline bool readFloat(const Json& object, std::string_view key, const std::string& path, float& out, Issues& issues,
                      float min = -std::numeric_limits<float>::max(), float max = std::numeric_limits<float>::max()) {
    const auto it = object.find(key);
    return it != object.end() && readFloatValue(*it, child(path, key), out, issues, min, max);
}

template <int N>
bool readVec(const Json& object, std::string_view key, const std::string& path, glm::vec<N, float>& out,
             Issues& issues, float min = -std::numeric_limits<float>::max(),
             float max = std::numeric_limits<float>::max()) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return false;
    }
    const std::string at = child(path, key);
    if (!it->is_array() || it->size() != static_cast<std::size_t>(N)) {
        issues.add(at, "expected an array of " + std::to_string(N) + " numbers");
        return false;
    }
    glm::vec<N, float> v(0.0f);
    bool ok = true;
    for (int i = 0; i < N; ++i) {
        ok = readFloatValue((*it)[static_cast<std::size_t>(i)], child(at, static_cast<std::size_t>(i)), v[i], issues,
                            min, max) &&
             ok;
    }
    if (ok) {
        out = v;
    }
    return ok;
}

inline bool readBool(const Json& object, std::string_view key, const std::string& path, bool& out, Issues& issues) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return false;
    }
    if (!it->is_boolean()) {
        issues.add(child(path, key), "expected true or false, got " + typeName(*it));
        return false;
    }
    out = it->get<bool>();
    return true;
}

inline bool readString(const Json& object, std::string_view key, const std::string& path, std::string& out,
                       Issues& issues, bool required = false) {
    const auto it = object.find(key);
    if (it == object.end()) {
        if (required) {
            issues.add(child(path, key), "required field is missing");
        }
        return false;
    }
    if (!it->is_string() || it->get_ref<const std::string&>().empty()) {
        issues.add(child(path, key), "expected a non-empty string, got " + (it->is_string() ? "\"\"" : typeName(*it)));
        return false;
    }
    out = it->get<std::string>();
    return true;
}

inline bool readUint(const Json& object, std::string_view key, const std::string& path, uint32_t& out,
                     Issues& issues) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return false;
    }
    if (!it->is_number_unsigned() || it->get<uint64_t>() > std::numeric_limits<uint32_t>::max()) {
        issues.add(child(path, key), "expected a non-negative integer, got " + typeName(*it));
        return false;
    }
    out = static_cast<uint32_t>(it->get<uint64_t>());
    return true;
}

template <int N>
Json vecJson(const glm::vec<N, float>& v) {
    Json array = Json::array();
    for (int i = 0; i < N; ++i) {
        array.push_back(v[i]);
    }
    return array;
}

// Material object <-> JSON (MaterialFile.cpp); shared with inline scene materials.
// `defaultName` is used when the object has no "name".
void readMaterialObject(const Json& object, const std::string& path, const std::string& defaultName,
                        MaterialData& out, Issues& issues);
Json materialJson(const MaterialData& material);

} // namespace zodar::detail
