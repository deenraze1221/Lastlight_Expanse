#include "Runtime/Application.h"
#include "Core/Error.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>

namespace zodar {
using Json = nlohmann::json;
SaveGameDesc Application::saveGameDesc() const {
    return {m_desc.identifier,1,{},inputLevelHash(),3,m_desc.autosaveInterval};
}
SaveGameData Application::captureGameSave() const { fail("application has no save handler"); }
std::unique_ptr<SaveCommit> Application::prepareGameSave(const SaveGameData&) { fail("application has no load handler"); }
SaveGameData Application::captureCheckpoint() const {
    if (!m_checkpoint) fail("save requires completed fixed-tick checkpoint");
    auto data=captureGameSave();
    const auto gameplay=m_gameplay.capture();
    if (gameplay.tick!=m_inputTick) fail("inconsistent checkpoint cursor");
    const auto stateHash=simulationFingerprint();
    if (m_playback) {
        const auto& expected=m_playback->ticks.at(m_inputTick-1).state;
        if (!expected.empty() && expected!=stateHash) fail("cannot save divergent replay checkpoint");
    }
    Json timers=Json::array(), events=Json::array();
    for (const auto& t:gameplay.timers.entries)
        timers.push_back({{"id",t.id},{"due",t.due},{"repeat",t.repeat},{"name",t.name},{"payload",t.payload}});
    for (const auto& b:gameplay.events)
        events.push_back({{"name",b.name},{"current",b.current},{"pending",b.pending}});
    data.tick=m_inputTick; data.seed=m_random.seed(); data.rng=m_random.states();
    data.runtime=Json{{"fixedDt",m_timestep.fixedDt()},{"input",Json::parse(m_input.capture())},
        {"cursor",m_inputTick},{"stateHash",stateHash},{"nextTimer",gameplay.timers.next},{"timers",timers},{"events",events},
        {"replayHash",m_playback ? inputHash(m_playback->serialize()) : std::string{}}}.dump();
    return data;
}
void Application::saveCheckpoint(const std::filesystem::path& path) {
    writeCheckpointFile(path,captureCheckpoint());
}
void Application::writeCheckpointFile(const std::filesystem::path& path, const SaveGameData& data) {
    const bool recovered=m_recoveredPrimary && std::filesystem::weakly_canonical(path)==*m_recoveredPrimary;
    writeSaveGame(path,saveGameDesc(),data,!recovered);
    if (recovered) m_recoveredPrimary.reset();
}
void Application::loadCheckpoint(const std::filesystem::path& path) {
    if ((!m_checkpoint && !m_starting) || m_desc.recordInput) fail("load requires startup/checkpoint and cannot replace an active recording");
    const auto settings=saveGameDesc();
    validateSaveGameDesc(settings);
    std::string errors;
    for (uint32_t i=0;i<=settings.backups;++i) {
        const std::filesystem::path candidate=i?std::filesystem::path(path.string()+".bak"+std::to_string(i)):path;
        try {
            std::ifstream in(candidate,std::ios::binary);
            if (!in) fail("cannot open save");
            auto data=parseSaveGame(std::string(std::istreambuf_iterator<char>(in),{}),settings,candidate);
            auto j=Json::parse(data.runtime);
            if (!j.is_object() || j.size()!=8 || !j.at("cursor").is_number_unsigned() ||
                j.at("cursor").get<uint64_t>()!=data.tick || !data.tick || data.tick==UINT64_MAX ||
                !j.at("nextTimer").is_number_unsigned() || data.seed!=m_desc.seed.value_or(0) || j.at("fixedDt")!=m_timestep.fixedDt() ||
                j.at("replayHash")!=(m_playback?inputHash(m_playback->serialize()):std::string{})) fail("incompatible saved runtime");
            if (m_playback && data.tick>m_desc.maxFrames) fail("saved replay cursor beyond end");
            auto stateHash=j.at("stateHash").get<std::string>();
            auto input=m_input.stage(j.at("input").dump());
            Gameplay::Snapshot snapshot{data.tick,{data.tick,j.at("nextTimer").get<uint64_t>(),{}},{}};
            if (!j.at("timers").is_array() || !j.at("events").is_array()) fail("invalid saved queues");
            for (const auto& t:j.at("timers")) {
                if (t.size()!=5 || !t.at("id").is_number_unsigned() || !t.at("due").is_number_unsigned() || !t.at("repeat").is_number_unsigned()) fail("invalid saved timer");
                snapshot.timers.entries.push_back({t.at("id").get<uint64_t>(),t.at("due").get<uint64_t>(),t.at("repeat").get<uint64_t>(),t.at("name").get<std::string>(),t.at("payload").get<std::string>()});
            }
            for (const auto& b:j.at("events")) {
                if (b.size()!=3) fail("invalid saved event batch");
                snapshot.events.push_back({b.at("name").get<std::string>(),b.at("current").get<std::vector<std::string>>(),b.at("pending").get<std::vector<std::string>>()});
            }
            auto gameplay=m_gameplay.stage(snapshot);
            Random random; random.restore(data.seed,data.rng);
            if (m_playback) {
                const auto& expected=m_playback->ticks.at(data.tick-1).state;
                if (!expected.empty() && expected!=stateHash) fail("saved checkpoint differs from replay state");
            }
            std::optional<std::filesystem::path> recoveredPrimary;
            if (i) recoveredPrimary=std::filesystem::weakly_canonical(path);
            auto exitSave=m_desc.saveAtExit ? std::optional<SaveGameData>(data) : std::nullopt;
            auto game=prepareGameSave(data);
            if (!game) fail("missing staged game state");
            if (simulationFingerprint(game->fingerprintState,random)!=stateHash)
                fail("saved checkpoint fingerprint does not match staged game/RNG state");
            m_renderer->waitIdle();
            if (i) ZODAR_LOG_WARN("Recovered save from {}",candidate.string());
            static_assert(std::is_nothrow_swappable_v<std::optional<SaveGameData>>);
            // Every fallible operation is above this point.
            game->commit(); m_input.commit(std::move(input)); m_gameplay.commit(std::move(gameplay));
            m_random=std::move(random); m_inputTick=data.tick; m_timestep.restoreCheckpoint(data.tick);
            m_exitSave.swap(exitSave); m_recoveredPrimary.swap(recoveredPrimary);
            m_inputStateHash.swap(stateHash); m_restored=true;
            return;
        } catch (const std::exception& e) { errors+=candidate.string()+": "+e.what()+"\n"; }
    }
    fail("load failed; live state unchanged:\n"+errors);
}
} // namespace zodar
