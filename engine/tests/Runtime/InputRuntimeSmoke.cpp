// Real Application-loop regressions: a skipped final render must not advance replay,
// and shutdown/capture failures must not replace an existing recording.
#include "Runtime/Application.h"
#include "Platform/Window.h"
#include "Core/Error.h"
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
        std::cout<<"Skipped-frame replay and failed-run recording preservation passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
