#include "SceneFile/SaveGame.h"
#include "SceneFile/JsonRead.h"
#include "Core/AtomicFile.h"
#include <cmath>
#include <fstream>
#include <limits>

namespace zodar {
namespace {
using Json = nlohmann::json; // Runtime clocks require double precision.
std::filesystem::path backup(const std::filesystem::path& path, uint32_t n) {
    return path.string() + ".bak" + std::to_string(n);
}
void checkDesc(const SaveGameDesc& desc) {
    if (desc.gameId.empty() || !std::isfinite(desc.autosaveInterval) || desc.autosaveInterval < 0 || desc.backups > 100)
        throw std::invalid_argument("invalid save descriptor");
}
}
std::string serializeSaveGame(const SaveGameDesc& desc, const SaveGameData& data) {
    checkDesc(desc);
    Json game = Json::object();
    for (const auto& [key, bytes] : data.game) game[key] = Json::parse(bytes);
    Json root = {{"schema", "zodar.save"}, {"version", 1}, {"gameId", desc.gameId},
        {"gameVersion", desc.gameVersion}, {"scenePath", desc.scenePath}, {"sceneHash", desc.sceneHash},
        {"seed", data.seed}, {"tick", data.tick}, {"rng", data.rng},
        {"world", Json::parse(serializeScene(data.world))}, {"game", game}, {"runtime", Json::parse(data.runtime)}};
    return root.dump(2) + "\n";
}
SaveGameData parseSaveGame(std::string_view text, const SaveGameDesc& desc, const std::filesystem::path& file) {
    checkDesc(desc);
    try {
        const auto j = Json::parse(text);
        const std::vector<std::string> keys{"schema","version","gameId","gameVersion","scenePath","sceneHash","seed","tick","rng","world","game","runtime"};
        if (!j.is_object() || j.size() != keys.size()) throw std::runtime_error("invalid save fields");
        for (const auto& key : keys) if (!j.contains(key)) throw std::runtime_error("missing save field: " + key);
        if (j.at("schema") != "zodar.save" || j.at("version") != 1 || j.at("gameId") != desc.gameId ||
            j.at("gameVersion") != desc.gameVersion || j.at("scenePath") != desc.scenePath || j.at("sceneHash") != desc.sceneHash)
            throw std::runtime_error("incompatible save header");
        if (!j.at("seed").is_number_unsigned() || !j.at("tick").is_number_unsigned() ||
            !j.at("rng").is_object() || !j.at("game").is_object()) throw std::runtime_error("invalid save state");
        SaveGameData data;
        data.seed = j.at("seed").get<uint64_t>(); data.tick = j.at("tick").get<uint64_t>();
        for (const auto& [name, value] : j.at("rng").items()) {
            if (!value.is_number_unsigned()) throw std::runtime_error("invalid RNG state");
            data.rng.emplace(name, value.get<uint64_t>());
        }
        data.world = parseScene(j.at("world").dump(), file.string(), file.parent_path());
        for (const auto& [name, value] : j.at("game").items()) data.game[name] = value.dump();
        data.runtime = j.at("runtime").dump();
        return data;
    } catch (const std::exception& e) { throw std::runtime_error(file.string() + ": " + e.what()); }
}
void writeSaveGame(const std::filesystem::path& path, const SaveGameDesc& desc, const SaveGameData& data) {
    const auto bytes = serializeSaveGame(desc, data);
    // Validate the generated document too (including hand-built component documents).
    (void)parseSaveGame(bytes, desc, path);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    // Backups are copied through the same exclusive-temp atomic writer. The primary
    // is never moved away; any failed backup operation leaves it untouched.
    if (desc.backups && std::filesystem::exists(path)) {
        const auto previous = detail::readTextFile(path);
        for (uint32_t i = desc.backups; i > 1; --i)
            if (std::filesystem::exists(backup(path, i - 1)))
                writeFileAtomically(backup(path, i), detail::readTextFile(backup(path, i - 1)));
        writeFileAtomically(backup(path, 1), previous);
    }
    writeFileAtomically(path, bytes);
}
StagedSaveGame loadSaveGame(const std::filesystem::path& path, const SaveGameDesc& desc,
                           const SceneBindings& bindings, bool recover) {
    checkDesc(desc);
    std::string errors;
    for (uint32_t i = 0; i <= (recover ? desc.backups : 0); ++i) {
        const auto candidate = i ? backup(path, i) : path;
        try {
            StagedSaveGame staged;
            staged.data = parseSaveGame(detail::readTextFile(candidate), desc, candidate);
            staged.instance = instantiateScene(staged.data.world, staged.world, bindings, candidate.string());
            if (i) staged.recoveredFrom = candidate;
            return staged;
        } catch (const std::exception& e) { errors += std::string(e.what()) + "\n"; }
    }
    throw std::runtime_error("no valid save: " + errors);
}
bool autosaveDue(const SaveGameDesc& desc, uint64_t previousTick, uint64_t tick, double fixedDt) {
    checkDesc(desc);
    if (!std::isfinite(fixedDt) || fixedDt <= 0 || tick < previousTick) throw std::invalid_argument("invalid autosave clock");
    if (!desc.autosaveInterval) return false;
    const double ticks = std::ceil(desc.autosaveInterval / fixedDt);
    if (ticks >= static_cast<double>(std::numeric_limits<uint64_t>::max())) return false;
    const auto interval = std::max(uint64_t{1}, static_cast<uint64_t>(ticks));
    return tick / interval > previousTick / interval;
}
} // namespace zodar
