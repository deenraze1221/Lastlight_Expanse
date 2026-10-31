#include "LoadingDemo.h"

#include "MockStartup.h"
#include "SdlInput.h"
#include "SdlRendererBackend.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "UI/Context.h"
#include "UI/Scale.h"
#include "UI/Screens/LoadingScreen.h"
#include "UI/Widgets/Button.h"
#include "UI/Widgets/Label.h"
#include "UI/Widgets/ProgressBar.h"
#include "UI/WindowPlan.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace zodar::sandbox {
namespace {

ui::Rect toRect(const SDL_Rect& r, float density) {
    return {r.x * density, r.y * density, r.w * density, r.h * density};
}

// Applies a pixel rectangle to the window (SDL positions/sizes are in window coordinates).
void placeWindow(SDL_Window* w, ui::Rect px, float density) {
    SDL_SetWindowSize(w, static_cast<int>(std::lround(px.width / density)), static_cast<int>(std::lround(px.height / density)));
    SDL_SetWindowPosition(w, static_cast<int>(std::lround(px.x / density)), static_cast<int>(std::lround(px.y / density)));
    SDL_SyncWindow(w);
}

std::string rectText(ui::Rect r) {
    return std::to_string(static_cast<int>(r.width)) + "x" + std::to_string(static_cast<int>(r.height)) + " at (" +
           std::to_string(static_cast<int>(r.x)) + ", " + std::to_string(static_cast<int>(r.y)) + ")";
}

} // namespace

int runLoadingDemo(const LoadingDemoOptions& opt, const CaptureFn& capture) {
    using namespace zodar::ui;
    if (!SDL_Init(SDL_INIT_VIDEO)) fail(std::string("SDL_Init failed: ") + SDL_GetError());

    LogBuffer log(500);
    MockStartupProgress mock(parseScenario(opt.scenario), &log);
    const std::string title = std::string("ZODAR UI Sandbox - MOCK startup data (") + opt.scenario + ")";

    // Borderless loading window; size and position are applied once the pixel density is known.
    SDL_Window* window = SDL_CreateWindow(title.c_str(), 960, 540,
                                          SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) fail(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    const float density = std::max(0.25f, SDL_GetWindowPixelDensity(window));

    DisplayArea area;
    if (opt.monitor) {
        area = {{0, 0, opt.monitor->width, opt.monitor->height}, {0, 0, opt.monitor->width, opt.monitor->height - 48}};
    } else {
        const SDL_DisplayID display = SDL_GetPrimaryDisplay();
        SDL_Rect bounds{}, usable{};
        SDL_GetDisplayBounds(display, &bounds);
        SDL_GetDisplayUsableBounds(display, &usable);
        area = {toRect(bounds, density), toRect(usable, density)};
    }
    const Rect loadingRect = loadingWindowRect(area);
    placeWindow(window, loadingRect, density);
    SDL_ShowWindow(window);
    ZODAR_LOG_INFO("Loading window (MOCK data, scenario '{}'): monitor {}x{} px -> {}", opt.scenario, area.bounds.width,
                   area.bounds.height, rectText(loadingRect));

    SDL_Renderer* renderer = SDL_CreateRenderer(window, opt.software ? "software" : nullptr);
    if (!renderer) fail(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    SDL_SetRenderVSync(renderer, 1);

    if (opt.at) mock.advance(*opt.at);

    TextureStore textures;
    FontLibrary fonts(opt.resources, textures);
    Context ctx(fonts);
    InputState input;
    DrawList list;
    SdlRendererBackend backend(renderer);
    LoadingScreen screen(opt.resources);
    if (opt.openLog) screen.setLogOpen(true);

    enum class Phase { Loading, Launcher } phase = Phase::Loading;
    Rect launcherRect;
    double completedFor = 0.0;
    int launcherFrames = 0;
    int exitCode = 0;
    Uint64 last = SDL_GetTicksNS();
    double realTime = 0.0;

    for (int frame = 1;; ++frame) {
        input.beginFrame();
        bool quit = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) quit = true;
            SDL_ConvertEventToRenderCoordinates(renderer, &e);
            if (auto ev = translateEvent(e)) input.apply(*ev);
        }
        if (quit) break;

        const Uint64 now = SDL_GetTicksNS();
        const float dt = static_cast<float>(static_cast<double>(now - last) * 1e-9);
        last = now;
        realTime += dt;
        if (!opt.at) mock.advance(dt * opt.speed);
        const double t = opt.at ? *opt.at : realTime;

        int pw = 0, ph = 0;
        SDL_GetRenderOutputSize(renderer, &pw, &ph);
        const Size2 pixels{static_cast<float>(pw), static_cast<float>(ph)};
        list.reset({0, 0, pixels.width, pixels.height});
        std::vector<PixelProbe> probes;

        if (phase == Phase::Loading) {
            const float scale = LoadingScreen::scaleFor(pixels);
            ctx.beginFrame(list, input, scale);
            const StartupSnapshot snap = mock.snapshot();
            // Frozen screenshots show the settled bar (no smoothing lag).
            const LoadingActions actions = screen.draw(ctx, textures, snap, &log, t, opt.at ? 1000.0f : dt, engineVersionLabel());
            ctx.endFrame();
            if (actions.retry) mock.retry();

            const LoadingLayout l = computeLoadingLayout(snap, screen.logOpen(), measureButton(ctx, screen.logOpen() ? "Hide log" : "Open log"),
                                                         measureButton(ctx, "Retry"));
            const int trackY = static_cast<int>(std::floor(l.track.y * scale + 0.5f));
            const int left = static_cast<int>(std::round(l.track.x * scale)) + 1;
            const int right = static_cast<int>(std::round(l.track.right() * scale)) - 2;
            if (!hasExactProgress(snap) && snap.state == StartupState::Running) {
                // Indeterminate: the sweeping segment is where the animation says it is.
                const Rect bar = ctx.toPixels(l.track);
                const Rect seg = indeterminateSegment(bar, t);
                const float cx = seg.x + seg.width * 0.5f;
                if (cx > bar.x + 2 && cx < bar.right() - 2) {
                    probes.push_back({"track.indeterminate", static_cast<int>(cx), trackY, color::kBrandAccent});
                    const float away = cx < bar.x + bar.width * 0.5f ? bar.right() - 3 : bar.x + 2;
                    probes.push_back({"track.indeterminate.rest", static_cast<int>(away), trackY, color::kBrandSurface});
                }
            }
            if (snap.state == StartupState::Failed) {
                probes.push_back({"track.failed", left, trackY, color::kBrandError});
            } else if (hasExactProgress(snap) && screen.displayedProgress() > 0.02f) {
                probes.push_back({"track.fill", left, trackY, color::kBrandAccent});
                if (screen.displayedProgress() < 0.98f) probes.push_back({"track.rest", right, trackY, color::kBrandSurface});
            }
            if (screen.logOpen()) {
                const Rect p = l.logPanel.scaled(scale);
                probes.push_back({"log.panel", static_cast<int>(p.right()) - 6, static_cast<int>(p.y) + 6, color::kBrandSurface});
            }

            if (snap.state == StartupState::Completed && !opt.at) {
                completedFor += dt;
                if (completedFor > 0.6) {
                    // Same window: becomes the launcher (bordered, resizable), resized and re-centred.
                    launcherRect = launcherWindowRect(area, SDL_GetWindowDisplayScale(window));
                    SDL_SetWindowBordered(window, true);
                    SDL_SetWindowResizable(window, true);
                    placeWindow(window, launcherRect, density);
                    phase = Phase::Launcher;
                    ZODAR_LOG_INFO("Loading finished: window resized and re-centred for the launcher -> {}", rectText(launcherRect));
                }
            }
        } else {
            const float scale = computeScale(ScalePolicy::Density, {pixels, SDL_GetWindowDisplayScale(window), 1.0f});
            ctx.beginFrame(list, input, scale);
            list.fillRect({0, 0, pixels.width, pixels.height}, color::kBrandBackground);
            label(ctx, {48, 44}, text::kOverline, color::kBrandAccent, "Project Launcher");
            label(ctx, {48, 64}, text::kDisplay, color::kBrandPrimary, "Launcher");
            label(ctx, {48, 112}, text::kBody, color::kTextSecondary,
                  "Placeholder: the launcher is Stage UI-3. This window is the same one that showed loading.");
            label(ctx, {48, 136}, text::kPath, color::kTextMuted,
                  "Window " + std::to_string(pw) + "x" + std::to_string(ph) + " px, planned " + rectText(launcherRect));
            ctx.endFrame();
            if (opt.exitAfterLauncher && ++launcherFrames >= 3) {
                const bool sizeOk = std::abs(pw - launcherRect.width) <= 2 && std::abs(ph - launcherRect.height) <= 2;
                ZODAR_LOG_INFO("Launcher window size check: {}x{} px vs planned {}: {}", pw, ph, rectText(launcherRect),
                               sizeOk ? "ok" : "MISMATCH");
                if (!sizeOk) exitCode = 5;
                backend.sync(textures);
                SDL_SetRenderDrawColor(renderer, 0x11, 0x11, 0x11, 0xFF);
                SDL_RenderClear(renderer);
                backend.render(list);
                const int c = capture(renderer, {});
                if (exitCode == 0) exitCode = c;
                SDL_RenderPresent(renderer);
                break;
            }
        }

        backend.sync(textures);
        SDL_SetRenderDrawColor(renderer, 0x11, 0x11, 0x11, 0xFF);
        SDL_RenderClear(renderer);
        backend.render(list);

        if (opt.frames > 0 && frame >= opt.frames) {
            exitCode = capture(renderer, probes);
            SDL_RenderPresent(renderer);
            break;
        }
        SDL_RenderPresent(renderer);
        if (opt.exitAfterLauncher && frame > 100000) {
            exitCode = 6; // Never reached the launcher.
            break;
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}

} // namespace zodar::sandbox
