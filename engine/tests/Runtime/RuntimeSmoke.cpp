// End-to-end check of zodar::Application: headless window (no SDL_VIDEO_DRIVER needed),
// deterministic fixed steps, hook order, a SceneRenderer pass, screenshot and exit code.
// Accepts the standard options (Runtime/CommandLine.h) on top of its defaults, and checks
// what they ask for: PNG/PPM screenshot content, --capture-frames files, --report JSON.
//   ZodarRuntimeSmoke --screenshot out.png [--capture-frames 3,6 --capture-dir DIR] [--report r.json]
// Exit code: the Application's (0 ok, 1 fatal, 2 validation, 3 screenshot) or 10 + n when
// check n fails.

#include "Core/Log.h"
#include "Platform/Platform.h"
#include "Runtime/Application.h"
#include "Runtime/CommandLine.h"

#include <stb_image.h>

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// Centre pixel of a PNG or PPM screenshot must show the blue clear colour.
bool screenshotLooksBlue(const std::filesystem::path& path) {
    const std::string data = readFile(path);
    int w = 0, h = 0, channels = 0;
    unsigned r = 0, g = 0, b = 0;
    if (data.rfind("P6", 0) == 0) {
        // Binary PPM (the engine's stb_image build has no PNM decoder).
        std::istringstream in(data);
        std::string magic;
        int maxValue = 0;
        in >> magic >> w >> h >> maxValue;
        in.get();
        if (!in || w <= 0 || h <= 0 || maxValue != 255) return false;
        const size_t centre = static_cast<size_t>(in.tellg()) +
                              (static_cast<size_t>(h / 2) * static_cast<size_t>(w) + static_cast<size_t>(w / 2)) * 3;
        if (centre + 3 > data.size()) return false;
        r = static_cast<uint8_t>(data[centre]);
        g = static_cast<uint8_t>(data[centre + 1]);
        b = static_cast<uint8_t>(data[centre + 2]);
    } else {
        stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                                static_cast<int>(data.size()), &w, &h, &channels, 3);
        if (!pixels || w <= 0 || h <= 0) {
            if (pixels) stbi_image_free(pixels);
            return false;
        }
        const size_t centre = (static_cast<size_t>(h / 2) * static_cast<size_t>(w) + static_cast<size_t>(w / 2)) * 3;
        r = pixels[centre];
        g = pixels[centre + 1];
        b = pixels[centre + 2];
        stbi_image_free(pixels);
    }
    std::printf("%s: %dx%d, centre pixel %u %u %u\n", path.string().c_str(), w, h, r, g, b);
    return b > r && b > 32;
}

std::filesystem::path capturePath(const zodar::ApplicationDesc& desc, uint64_t frame) {
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%06llu.png", static_cast<unsigned long long>(frame));
    return desc.captureDir / name;
}

bool isPng(const std::filesystem::path& path) {
    return readFile(path).substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8);
}

int check(bool ok, int n, const char* what) {
    if (!ok) ZODAR_LOG_ERROR("Runtime smoke check {} failed: {}", n, what);
    return ok ? 0 : 10 + n;
}

} // namespace

int main(int argc, char** argv) {
    zodar::log::init({spdlog::level::info, ""});
    int result = 1;
    try {
        zodar::ApplicationDesc desc;
        desc.name = "ZodarRuntimeSmoke";
        desc.width = 320;
        desc.height = 180;
        desc.headless = true;
        desc.deterministic = true;
        desc.maxFrames = kFrames;
        desc.screenshot = "runtime_headless.png";
        desc.threads = 1;
        const std::vector<std::string> args(argv + 1, argv + argc);
        const std::vector<std::string> rest = zodar::parseStandardOptions(args, desc);
        if (!rest.empty()) throw std::runtime_error("Unknown argument '" + rest.front() + "'");
        const uint64_t frames = desc.maxFrames;
        std::filesystem::remove(*desc.screenshot);
        if (desc.reportPath) std::filesystem::remove(*desc.reportPath);
        for (uint64_t f : desc.captureFrames) std::filesystem::remove(capturePath(desc, f));

        SmokeApp app(desc);
        const char* driver = app.platform().videoDriver();
        result = app.run();
        if (result == 0) result = check(std::string_view(driver) == "offscreen", 1, "video driver is not offscreen");
        if (result == 0) result = check(app.startOrder == 0 && app.shutdownOrder == 1, 2, "hook order");
        if (result == 0) result = check(app.updates >= frames && app.fixedSteps == app.updates, 3, "fixed step count");
        if (result == 0) result = check(app.renders == frames && app.renderer().frameNumber() == frames, 4, "frame count");
        if (result == 0) result = check(app.timeOk, 5, "deterministic frame time");
        if (result == 0) result = check(screenshotLooksBlue(*desc.screenshot), 6, "screenshot content");
        for (uint64_t f : desc.captureFrames) {
            const std::filesystem::path capture = capturePath(desc, f);
            if (result == 0) result = check(isPng(capture) && screenshotLooksBlue(capture), 7, "capture file");
        }
        if (result == 0 && desc.reportPath) {
            const std::string report = readFile(*desc.reportPath);
            const bool ok = report.find("\"schema\": \"zodar.run_report\"") != std::string::npos &&
                            report.find("\"frames\": " + std::to_string(frames) + ",") != std::string::npos &&
                            report.find("\"exitCode\": 0,") != std::string::npos &&
                            report.find("\"errors\": 0, \"warnings\"") != std::string::npos &&
                            report.find("\"cpuScopes\": {\n") != std::string::npos &&
                            report.find(desc.screenshot->string()) != std::string::npos;
            result = check(ok, 8, "run report content");
        }
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
        result = 1;
    }
    ZODAR_LOG_INFO("Runtime smoke exit code {}", result);
    zodar::log::shutdown();
    return result;
}
