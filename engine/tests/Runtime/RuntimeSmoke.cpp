// End-to-end check of zodar::Application: headless window (no SDL_VIDEO_DRIVER needed),
// deterministic fixed steps, hook order, a SceneRenderer pass, screenshot and exit code.
//   ZodarRuntimeSmoke --screenshot out.ppm
// Exit code: the Application's (0 ok, 1 fatal, 2 validation, 3 screenshot) or 10 + n when
// check n fails.

#include "Core/Log.h"
#include "Platform/Platform.h"
#include "Runtime/Application.h"

#include <SDL3/SDL_main.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr uint64_t kFrames = 12;

class SmokeApp final : public zodar::Application {
public:
    explicit SmokeApp(zodar::ApplicationDesc desc) : Application(std::move(desc)) {}

    int order = 0;
    int startOrder = -1, shutdownOrder = -1;
    uint64_t fixedSteps = 0, updates = 0, renders = 0;
    double lastTime = -1.0;
    bool timeOk = true;

protected:
    void onStart() override { startOrder = order++; }
    void onFixedUpdate(double dt) override {
        ++fixedSteps;
        if (dt != timestep().fixedDt()) timeOk = false;
    }
    void onUpdate(const zodar::FrameTime& t) override {
        // Deterministic: exactly one fixed step and dt == fixedDt every frame.
        if (t.fixedSteps != 1 || t.dt != timestep().fixedDt() || t.frame != updates) timeOk = false;
        if (std::abs(t.time - static_cast<double>(updates) * timestep().fixedDt()) > 1e-9 || t.time <= lastTime) timeOk = false;
        lastTime = t.time;
        ++updates;
    }
    void onRender(zodar::RenderContext& rc) override {
        zodar::gfx::SceneView view;
        view.clearColor = {0.1f, 0.3f, 0.8f, 1.0f};
        scene().addToGraph(rc.graph, rc.info, rc.extent, view, rc.output, rc.format, &rc.stats);
        ++renders;
    }
    void onShutdown() override { shutdownOrder = order++; }
};

bool screenshotLooksBlue(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    uint32_t w = 0, h = 0, maxValue = 0;
    in >> magic >> w >> h >> maxValue;
    in.get();
    if (!in || magic != "P6" || w == 0 || h == 0 || maxValue != 255) return false;
    in.seekg(static_cast<std::streamoff>((size_t(h / 2) * w + w / 2) * 3), std::ios::cur);
    unsigned char rgb[3] = {};
    in.read(reinterpret_cast<char*>(rgb), 3);
    std::printf("centre pixel %u %u %u\n", rgb[0], rgb[1], rgb[2]);
    return in && rgb[2] > rgb[0] && rgb[2] > 32;
}

int check(bool ok, int n, const char* what) {
    if (!ok) ZODAR_LOG_ERROR("Runtime smoke check {} failed: {}", n, what);
    return ok ? 0 : 10 + n;
}

} // namespace

int main(int argc, char** argv) {
    zodar::log::init({spdlog::level::info, ""});
    std::filesystem::path shot = "runtime_headless.ppm";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--screenshot") shot = argv[++i];
    }
    std::filesystem::remove(shot);

    int result = 1;
    try {
        zodar::ApplicationDesc desc;
        desc.name = "ZodarRuntimeSmoke";
        desc.width = 320;
        desc.height = 180;
        desc.headless = true;
        desc.deterministic = true;
        desc.maxFrames = kFrames;
        desc.screenshot = shot;
        desc.threads = 1;
        SmokeApp app(desc);
        const char* driver = app.platform().videoDriver();
        result = app.run();
        if (result == 0) result = check(std::string_view(driver) == "offscreen", 1, "video driver is not offscreen");
        if (result == 0) result = check(app.startOrder == 0 && app.shutdownOrder == 1, 2, "hook order");
        if (result == 0) result = check(app.updates >= kFrames && app.fixedSteps == app.updates, 3, "fixed step count");
        if (result == 0) result = check(app.renders == kFrames && app.renderer().frameNumber() == kFrames, 4, "frame count");
        if (result == 0) result = check(app.timeOk, 5, "deterministic frame time");
        if (result == 0) result = check(screenshotLooksBlue(shot), 6, "screenshot content");
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
        result = 1;
    }
    ZODAR_LOG_INFO("Runtime smoke exit code {}", result);
    zodar::log::shutdown();
    return result;
}
