// Real Application-loop regressions: a skipped final render must not advance replay,
// and shutdown/capture failures must not replace an existing recording.
#include "Runtime/Application.h"
#include "Platform/Window.h"
#include "Core/Error.h"
#include "Core/AtomicFile.h"
#include "Core/Log.h"
#include <SDL3/SDL.h>
#include "Debug/DebugOverlay.h"
#include <imgui.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
class Smoke final : public zodar::Application {
public:
    enum class Mode { SkipFinal, FailShutdown, Normal, UICapture, Execution, CatchUp };
    Smoke(zodar::ApplicationDesc desc, Mode mode) : Application(std::move(desc)), m_mode(mode) {}
    unsigned steps=0,updates=0,renders=0;
    float position=0;
    double finalTime=-1;
    unsigned checkpoints=0;
    bool sawCatchUp=false;
protected:
    struct Pulse { uint64_t tick; };
    bool execution() const { return m_mode==Mode::Execution || m_mode==Mode::CatchUp; }
    void onStart() override {
        if(execution()) {
            using zodar::GameplayPhase;
            gameplay().systems().add({"probe.Input", GameplayPhase::Input, [this](const auto& c) {
                ZODAR_ENSURE(m_stage==0 && c.tick==steps+1 && c.alpha==0, "input phase order/tick");
                if(m_mode==Mode::Execution) ZODAR_ENSURE(input().action("move").x==1, "system ran before input injection");
                const auto events=gameplay().events().read<Pulse>();
                ZODAR_ENSURE(c.tick==1 ? events.empty() : events.size()==1 && events[0].tick==c.tick-1, "timer event delivery is not N+1");
                m_stage=1;
            }});
            gameplay().systems().add({"probe.Fixed", GameplayPhase::FixedSimulation, [this](const auto&) {
                ZODAR_ENSURE(m_stage==1,"fixed phase order"); m_stage=2;
            }});
            gameplay().systems().add({"probe.Animation", GameplayPhase::Animation, [this](const auto&) {
                ZODAR_ENSURE(m_stage==2,"animation phase order"); m_stage=3;
            }});
            gameplay().systems().add({"probe.Late", GameplayPhase::LateUpdate, [this](const auto& c) {
                ZODAR_ENSURE(m_stage==0 && c.tick==steps && checkpoints==steps,"late update preceded checkpoint");
                ZODAR_ENSURE(m_frameStage==0 || m_frameStage==3,"late phase repeated"); m_frameStage=1;
            }});
            gameplay().systems().add({"probe.Sync", GameplayPhase::RenderSync, [this](const auto&) {
                ZODAR_ENSURE(m_frameStage==2,"render sync preceded update"); m_frameStage=3;
            }});
            gameplay().timers().after(1,[this] {
                ZODAR_ENSURE(m_stage==4,"timer preceded fixed hook");
                ++m_timerCalls; gameplay().events().emit(Pulse{gameplay().timers().tick()});
            },1);
        }
        if(m_mode!=Mode::UICapture) return;
        overlay().visible=true;
        ImGui::GetIO().WantCaptureMouse=true;
        ImGui::GetIO().WantCaptureKeyboard=true;
        input().feed("key","Space",1); input().beginStep();
        SDL_Event event{}; event.type=SDL_EVENT_KEY_UP; event.key.scancode=SDL_SCANCODE_SPACE;
        SDL_PushEvent(&event);
        event={}; event.type=SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button=SDL_BUTTON_LEFT;
        SDL_PushEvent(&event);
    }
    void onFixedUpdate(double) override {
        ++steps; position+=input().action("move").x;
        if(execution()) { ZODAR_ENSURE(m_stage==3,"legacy fixed hook order"); m_stage=4; }
        if(m_mode==Mode::UICapture && steps==1 &&
           (input().action("jump").held || !input().action("jump").released || input().action("dig").pressed))
            zodar::fail("overlay input reached gameplay or swallowed release");
    }
    void onCheckpoint(uint64_t tick) override {
        if(!execution()) return;
        const auto ready=gameplay().events().read<Pulse>();
        ZODAR_ENSURE(m_stage==4 && tick==steps && m_timerCalls==steps && ready.size()==1 && ready[0].tick==tick,
                     "incomplete checkpoint");
        ++checkpoints; m_stage=0;
    }
    void onUpdate(const zodar::FrameTime& t) override {
        ++updates;
        if(execution()) {
            ZODAR_ENSURE(m_frameStage==1,"update preceded late phase"); m_frameStage=2;
            sawCatchUp |= steps-m_previousFrameSteps>=2; m_previousFrameSteps=steps;
        }
        if(m_mode==Mode::CatchUp && t.frame==0) SDL_Delay(60);
        if(m_mode!=Mode::SkipFinal && m_mode!=Mode::Execution) return;
        if(t.frame==3) finalTime=t.time;
        if(t.frame==4 && t.time!=finalTime) zodar::fail("final replay render changed simulation time");
        SDL_Event event{};
        if(t.frame==2) event.type=SDL_EVENT_WINDOW_MINIMIZED;
        else if(t.frame==3) event.type=SDL_EVENT_WINDOW_RESTORED;
        else return;
        event.window.windowID=SDL_GetWindowID(window().native());
        if(!SDL_PushEvent(&event)) zodar::fail("cannot inject window event");
    }
    void onRender(zodar::RenderContext& rc) override {
        ++renders;
        if(execution()) ZODAR_ENSURE(m_frameStage==3,"render preceded sync");
        zodar::gfx::SceneView view;
        scene().addToGraph(rc.graph,rc.info,rc.extent,view,rc.output,rc.format,&rc.stats);
    }
    void onShutdown() override { if(m_mode==Mode::FailShutdown) zodar::fail("injected shutdown failure"); }
private:
    Mode m_mode;
    unsigned m_stage=0,m_frameStage=0,m_timerCalls=0,m_previousFrameSteps=0;
};
// Exercises the real Application transaction, including failures after world staging.
class SaveProbe final : public zodar::Application {
public:
    SaveProbe(zodar::ApplicationDesc desc, std::filesystem::path root, bool rollback)
        : Application(std::move(desc)), m_root(std::move(root)), m_rollback(rollback) {}
    bool checked=false, restored=false, midTickRejected=false;
    double firstUpdateTime=-1, restoredTime=-1;
    std::string fingerprint() {
        auto s=gameplay().capture();
        nlohmann::json timers=nlohmann::json::array(),events=nlohmann::json::array();
        for (const auto& t:s.timers.entries) timers.push_back({t.id,t.due,t.repeat,t.name,t.payload});
        for (const auto& e:s.events) events.push_back({e.name,e.current,e.pending});
        return nlohmann::json{{"game",nlohmann::json::parse(captureGameSave().game.at("probe"))},
            {"world",zodar::serializeScene(zodar::captureScene(m_world,{}))},
            {"input",input().capture()},{"rng",random().states()},{"seed",random().seed()},
            {"tick",s.tick},{"clock",timestep().totalSteps()},{"nextTimer",s.timers.next},
            {"timers",timers},{"events",events}}.dump();
    }
protected:
    struct Pulse { uint64_t value; };
    zodar::SaveGameDesc saveGameDesc() const override {
        auto d=Application::saveGameDesc(); d.backups=m_badDesc?101:0; return d;
    }
    std::string inputState() const override { return std::to_string(m_value)+":"+std::to_string(m_steps)+":"+std::to_string(m_timerCalls); }
    void onStart() override {
        std::filesystem::create_directories(m_root);
        m_entity=m_world.create(); m_world.add<zodar::SceneId>(m_entity,zodar::SceneId{"probe"});
        m_world.add<zodar::Transform>(m_entity);
        gameplay().events().registerType<Pulse>("pulse",[](const Pulse& p){return std::to_string(p.value);},
            [](std::string_view s) { size_t end=0; const auto n=std::stoull(std::string(s),&end);
                if (end!=s.size()) throw std::runtime_error("invalid pulse");
                return Pulse{n}; });
        gameplay().timers().registerCallback("pulse",[this](std::string_view) {
            ++m_timerCalls; m_value+=3; gameplay().events().emit(Pulse{gameplay().timers().tick()});
        });
        gameplay().timers().afterNamed(2,"pulse",{},2);
        std::filesystem::remove(m_root/"mid-tick");
    }
    void onFixedUpdate(double) override {
        ++m_steps; m_value+=random().next("probe")%100;
        m_world.get<zodar::Transform>(m_entity).position.x=static_cast<float>(m_steps);
        gameplay().events().emit(Pulse{m_steps});
        try { saveCheckpoint(m_root/"mid-tick"); }
        catch (const std::exception&) { midTickRejected=true; }
        if (!midTickRejected || std::filesystem::exists(m_root/"mid-tick")) zodar::fail("mid-tick save was published");
    }
    zodar::SaveGameData captureGameSave() const override {
        zodar::SaveGameData d; d.world=zodar::captureScene(m_world,{});
        d.game["probe"]=nlohmann::json{{"steps",m_steps},{"value",m_value},{"timerCalls",m_timerCalls}}.dump();
        return d;
    }
    std::unique_ptr<zodar::SaveCommit> prepareGameSave(const zodar::SaveGameData& d) override {
        auto world=std::make_shared<zodar::World>();
        auto instance=zodar::instantiateScene(d.world,*world,{});
        const auto entity=instance.byId.at("probe");
        const auto j=nlohmann::json::parse(d.game.at("probe"));
        if (j.size()!=3 || !j.at("steps").is_number_unsigned() || !j.at("value").is_number_unsigned() ||
            !j.at("timerCalls").is_number_unsigned()) zodar::fail("invalid staged probe game");
        const auto steps=j.at("steps").get<uint64_t>(),value=j.at("value").get<uint64_t>(),calls=j.at("timerCalls").get<uint64_t>();
        return zodar::makeSaveCommit([this,world,entity,steps,value,calls]() noexcept {
            m_world=std::move(*world); m_entity=entity; m_steps=steps; m_value=value; m_timerCalls=calls;
        },std::to_string(value)+":"+std::to_string(steps)+":"+std::to_string(calls));
    }
    void onUpdate(const zodar::FrameTime& time) override {
        if (firstUpdateTime<0) firstUpdateTime=time.time;
        if (restored && restoredTime<0) restoredTime=time.time;
    }
    void onCheckpoint(uint64_t tick) override {
        if (tick==2 && !m_saved) {
            gameplay().events().emit(Pulse{100}); // Persist the pending batch as well as current events.
            saveCheckpoint(m_root/"good"); m_savedState=fingerprint(); m_saved=true;
        }
        if (m_rollback && tick==3 && !checked) {
            std::ifstream in(m_root/"good"); const auto good=nlohmann::json::parse(in);
            const auto before=fingerprint();
            for (unsigned variant=0;variant<10;++variant) {
                auto bad=good;
                if (variant==0) bad["version"]=2;
                if (variant==1) bad["rng"]["probe"]=-1;
                if (variant==2) bad["runtime"]["cursor"]=-1;
                if (variant==3) bad["runtime"]["timers"][0]["name"]="unknown";
                if (variant==4) bad["runtime"]["events"][0]["name"]="unknown";
                if (variant==5) bad["runtime"]["input"]["state"] = nlohmann::json::object();
                if (variant==6) bad["game"]["probe"]="invalid after world staging";
                if (variant==7) bad["rng"]["probe"]=good.at("rng").at("probe").get<uint64_t>()^uint64_t{1};
                if (variant==8) {
                    auto game=nlohmann::json::parse(bad["game"]["probe"].dump());
                    game["value"]=game.at("value").get<uint64_t>()+1;
                    bad["game"]["probe"]=game;
                }
                if (variant==9) bad["runtime"]["stateHash"]="wrong hash";
                zodar::writeFileAtomically(m_root/"bad",bad.dump());
                bool failed=false;
                try { loadCheckpoint(m_root/"bad"); } catch (const std::exception&) { failed=true; }
                if (!failed || fingerprint()!=before) zodar::fail("failed load changed live simulation state");
            }
            m_badDesc=true; bool rejected=false;
            try { loadCheckpoint(m_root/"good"); } catch (const std::exception&) { rejected=true; }
            m_badDesc=false;
            if (!rejected || fingerprint()!=before) zodar::fail("invalid save descriptor changed state");
            checked=true;
        }
        if (m_rollback && tick==4 && !restored) {
            loadCheckpoint(m_root/"good"); restored=true;
            if (fingerprint()!=m_savedState) zodar::fail("checkpoint did not restore world/input/RNG/queues/clocks");
        } else if (tick==6) requestQuit();
    }
    void onRender(zodar::RenderContext& rc) override {
        zodar::gfx::SceneView view;
        scene().addToGraph(rc.graph,rc.info,rc.extent,view,rc.output,rc.format,&rc.stats);
    }
private:
    std::filesystem::path m_root;
    zodar::World m_world;
    zodar::Entity m_entity;
    bool m_rollback=false,m_saved=false,m_badDesc=false;
    uint64_t m_steps=0,m_value=0,m_timerCalls=0;
    std::string m_savedState;
};
std::string read(const std::filesystem::path& p) {
    std::ifstream in(p,std::ios::binary); return std::string(std::istreambuf_iterator<char>(in),{});
}
void require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
}
int main(int argc,char** argv) {
    zodar::log::init({spdlog::level::info,""});
    try {
        require(argc==2,"needs output directory");
        const std::filesystem::path root=argv[1]; std::filesystem::create_directories(root);
        const auto mapPath=root/"map.json",recordPath=root/"record.jsonl";
        const auto map=zodar::InputMap::parse(R"({"schema":"zodar.input_map","version":1,"contexts":[{"name":"test","actions":{"move":{"type":"axis1","bindings":[]},"jump":{"type":"button","bindings":[{"source":"key","code":"Space"}]},"dig":{"type":"button","bindings":[{"source":"mouseButton","code":"left"}]}}}]})");
        { std::ofstream out(mapPath); out<<map.serialize(); }
        zodar::InputRecording rec;
        rec.header={"ZodarInputRuntimeSmoke",map.hash(),"",42,1.0/60.0};
        for(int i=0;i<4;++i) rec.ticks.push_back({{{"move",{1,0,false,false,false}},{"jump",{}},{"dig",{}}},""});
        rec.save(recordPath);
        zodar::ApplicationDesc d;
        d.identifier="ZodarInputRuntimeSmoke"; d.headless=true; d.width=64; d.height=64; d.threads=0;
        d.seed=42; d.inputMap=mapPath; d.replayInput=recordPath; d.screenshot=root/"final.png";
        {
            Smoke app(d,Smoke::Mode::SkipFinal);
            require(app.run()==0,"skipped-frame replay failed");
            require(app.steps==4 && app.position==4 && app.updates==5 && app.renders==4,"replay advanced while final render was pending");
            require(std::filesystem::file_size(*d.screenshot)>0,"final replay screenshot missing");
        }
        d.reportPath=root/"execution-report.json";
        {
            Smoke app(d,Smoke::Mode::Execution);
            require(app.run()==0,"system execution with skipped rendering failed");
            require(app.steps==4 && app.checkpoints==4 && app.updates==5 && app.renders==4,"execution/checkpoint duplicated");
            std::ifstream report(*d.reportPath); const auto data=nlohmann::json::parse(report);
            for(const auto* name : {"Input","Fixed","Animation"})
                require(data["cpuScopes"][std::string("System/probe.")+name]["count"]==4,"skipped frame lost fixed system scope");
            for(const auto* name : {"Late","Sync"})
                require(data["cpuScopes"][std::string("System/probe.")+name]["count"]==5,"skipped frame lost presentation scope");
        }
        auto catchUp=d; catchUp.replayInput.reset(); catchUp.maxFrames=3; catchUp.deterministic=false;
        catchUp.screenshot.reset(); catchUp.reportPath.reset();
        {
            Smoke app(catchUp,Smoke::Mode::CatchUp);
            require(app.run()==0 && app.sawCatchUp && app.steps==app.checkpoints,"multiple ticks per frame failed");
        }
        d.reportPath.reset();
        const auto previous=read(recordPath);
        d.replayInput.reset(); d.recordInput=recordPath; d.maxFrames=2; d.deterministic=true; d.screenshot.reset();
        {
            Smoke app(d,Smoke::Mode::FailShutdown);
            require(app.run()==1,"shutdown failure was not reported");
            require(read(recordPath)==previous,"shutdown failure replaced recording");
        }
        d.screenshot=root/"blocked.png"; std::filesystem::create_directories(*d.screenshot);
        {
            Smoke app(d,Smoke::Mode::Normal);
            require(app.run()==3,"capture failure was not reported");
            require(read(recordPath)==previous,"capture failure replaced recording");
        }
        d.recordInput.reset(); d.screenshot.reset();
        {
            Smoke app(d,Smoke::Mode::UICapture);
            require(app.run()==0,"overlay input regression failed");
        }
        auto saveDesc=d; saveDesc.maxFrames=8; saveDesc.deterministic=true;
        std::string baseline;
        {
            SaveProbe app(saveDesc,root/"save-baseline",false);
            require(app.run()==0 && app.midTickRejected,"save baseline failed"); baseline=app.fingerprint();
        }
        {
            SaveProbe app(saveDesc,root/"save-rollback",true);
            require(app.run()==0 && app.checked && app.restored,"transactional rollback checks failed");
            require(app.fingerprint()==baseline,"restored events/timers/input/RNG/game/world diverged from baseline");
            require(app.restoredTime==saveDesc.timestep.fixedDt,"mid-checkpoint load lost visual simulation time");
        }
        {
            auto loaded=saveDesc; loaded.loadSave=root/"save-baseline/good";
            SaveProbe app(loaded,root/"save-startup",false);
            require(app.run()==0,"startup checkpoint load failed");
            require(app.firstUpdateTime==2*loaded.timestep.fixedDt,"startup load reset the live frame clock");
            require(app.fingerprint()==baseline,"startup continuation differs from uninterrupted simulation");
        }
        saveDesc.saveAtExit=root/"blocked-save"; std::filesystem::create_directories(*saveDesc.saveAtExit);
        saveDesc.reportPath=root/"save-failure-report.json";
        {
            SaveProbe app(saveDesc,root/"save-write-failure",false);
            require(app.run()==1,"failed exit save was not reported");
            const auto report=nlohmann::json::parse(read(*saveDesc.reportPath));
            require(report.at("exitCode")==1,"run report claims success after exit-save failure");
        }
        std::cout<<"Skipped-frame replay and failed-run recording preservation passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
