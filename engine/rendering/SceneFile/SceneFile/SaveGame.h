#pragma once
#include "SceneFile/SceneFile.h"
#include <map>

namespace zodar {
struct SaveGameDesc {
    std::string gameId;
    uint32_t gameVersion = 1;
    std::string scenePath, sceneHash;
    uint32_t backups = 3;
    double autosaveInterval = 0; // Fixed simulation seconds; zero disables autosave.
};
// All strings in game/runtime are JSON values, validated before disk writes.
struct SaveGameData {
    uint64_t seed = 0, tick = 0;
    std::map<std::string, uint64_t> rng;
    SceneDocument world;
    std::map<std::string, std::string> game;
    std::string runtime = "{}";
};
struct StagedSaveGame {
    SaveGameData data;
    World world;
    SceneInstance instance;
    std::filesystem::path recoveredFrom; // Nonempty only when a backup was used.
};
// CPU staging only. Validate application handlers against this new world before committing
// with noexcept swaps. Bindings must outlive the restored world. Never mutates a live world.
StagedSaveGame loadSaveGame(const std::filesystem::path& path, const SaveGameDesc& desc,
                            const SceneBindings& bindings, bool recover = true);
// Caller must supply a completed fixed-tick checkpoint. Runtime integration enforces this.
// Temp files and backup preparation happen before atomic replacement; errors throw.
void writeSaveGame(const std::filesystem::path& path, const SaveGameDesc& desc, const SaveGameData& data);
std::string serializeSaveGame(const SaveGameDesc& desc, const SaveGameData& data);
SaveGameData parseSaveGame(std::string_view text, const SaveGameDesc& desc,
                           const std::filesystem::path& file);
bool autosaveDue(const SaveGameDesc& desc, uint64_t previousTick, uint64_t tick, double fixedDt);
} // namespace zodar
