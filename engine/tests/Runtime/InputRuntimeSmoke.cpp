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

namespace {
class Smoke final : public zodar::Application {
public:
    enum class Mode { SkipFinal, FailShutdown, Normal, UICapture };
    Smoke(zodar::ApplicationDesc desc, Mode mode) : Application(std::move(desc)), m_mode(mode) {}
    unsigned steps=0,updates=0,renders=0;
    float position=0;
    double finalTime=-1;
protected:
    void onStart() override {
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
        if(m_mode==Mode::UICapture && steps==1 &&
           (input().action("jump").held || !input().action("jump").released || input().action("dig").pressed))
            zodar::fail("overlay input reached gameplay or swallowed release");
    }
    void onUpdate(const zodar::FrameTime& t) override {
        ++updates;
        if(m_mode!=Mode::SkipFinal) return;
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
        zodar::gfx::SceneView view;
        scene().addToGraph(rc.graph,rc.info,rc.extent,view,rc.output,rc.format,&rc.stats);
    }
    void onShutdown() override { if(m_mode==Mode::FailShutdown) zodar::fail("injected shutdown failure"); }
private:
    Mode m_mode;
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
