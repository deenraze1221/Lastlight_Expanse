// ZODAR UI Sandbox (TEMPORARY): component gallery for Stage 1 review. Draws the Zodar::UI
// draw lists with SDL3's 2D renderer. Everything shown is real UI-module output; no engine
// progress or project data is displayed.
#include "SdlInput.h"
#include "SdlRendererBackend.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "UI/Context.h"
#include "UI/Layout.h"
#include "UI/Scale.h"
#include "UI/Svg.h"
#include "UI/Widgets/Button.h"
#include "UI/Widgets/Image.h"
#include "UI/Widgets/Label.h"

#include <SDL3/SDL.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace zodar;
using namespace zodar::ui;

namespace {

struct Options {
    std::optional<float> displayScale; // --scale: overrides the OS display scale (test profiles).
    float userScale = 1.0f;
    std::optional<std::pair<int, int>> size; // --size WxH in pixels.
    int frames = 0;
    std::string screenshot;
    bool check = false;
    bool software = false;
    std::string resources;
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) fail("missing value for " + a);
            return argv[++i];
        };
        if (a == "--scale") o.displayScale = std::stof(next());
        else if (a == "--user-scale") o.userScale = std::stof(next());
        else if (a == "--size") {
            const std::string v = next();
            int w = 0, h = 0;
            if (std::sscanf(v.c_str(), "%dx%d", &w, &h) != 2) fail("--size expects WxH");
            o.size = std::make_pair(w, h);
        } else if (a == "--frames") o.frames = std::stoi(next());
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--check") o.check = true;
        else if (a == "--software") o.software = true;
        else if (a == "--resources") o.resources = next();
        else if (a == "--help") {
            std::printf("ZodarUISandbox [--scale F] [--user-scale F] [--size WxH] [--frames N] [--screenshot F.png]\n"
                        "               [--check] [--software] [--resources DIR]\n");
            std::exit(0);
        } else fail("unknown argument: " + a);
    }
    return o;
}

std::filesystem::path resourceDir(const Options& o) {
    if (!o.resources.empty()) return o.resources;
    if (const char* env = std::getenv("ZODAR_UI_DIR")) return env;
    const char* base = SDL_GetBasePath();
    return std::filesystem::path(base ? base : ".") / "ui";
}

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}

std::string hex(Color c) {
    char b[16];
    std::snprintf(b, sizeof(b), "#%06X", c.rgb());
    return b;
}

// Logos rasterized at the current pixel density (re-rasterized when the scale changes).
struct Logos {
    SvgDocument icon;
    SvgDocument lockup;
    // Content boxes inside the original 512x512 documents (SVG units).
    static constexpr Rect kIconBox{59, 87, 394, 338};
    static constexpr Rect kLockupBox{32, 110, 450, 290};
    float rasterScale = 0;
    TextureId iconSmall = kNoTexture, iconLarge = kNoTexture, lockupTex = kNoTexture;

    void ensure(TextureStore& store, float uiScale) {
        if (rasterScale == uiScale) return;
        if (iconSmall) { store.remove(iconSmall); store.remove(iconLarge); store.remove(lockupTex); }
        // Header icon: 42x36 design units; large icon: 96 units tall; lockup: 225 units wide.
        iconSmall = store.add(icon.rasterize(42.0f * uiScale / kIconBox.width, kIconBox));
        iconLarge = store.add(icon.rasterize(112.0f * uiScale / kIconBox.width, kIconBox));
        lockupTex = store.add(lockup.rasterize(225.0f * uiScale / kLockupBox.width, kLockupBox));
        rasterScale = uiScale;
    }
};

struct Probe {
    std::string name;
    Vec2 pixel;
    Color expected;
};

// Builds and draws the gallery. Layout in design units via LayoutTree.
void buildGallery(Context& ctx, Logos& logos, Size2 logical, float scroll, const std::string& footer,
                  std::vector<Probe>& probes) {
    probes.clear();
    DrawList& dl = ctx.draw();
    const float s = ctx.scale();
    dl.fillRect({0, 0, logical.width * s + 1, logical.height * s + 1}, color::kBrandBackground);

    LayoutTree t;
    NodeStyle page;
    page.widthSizing = Sizing::Fixed;
    page.size = {logical.width, 0};
    page.padding = {48, 36, 48, 40};
    page.gap = 32;
    const NodeId root = t.add(kNoNode, page);

    auto leaf = [&](NodeId parent, Size2 size) {
        NodeStyle st;
        st.widthSizing = Sizing::Fixed;
        st.heightSizing = Sizing::Fixed;
        st.size = size;
        return t.add(parent, st);
    };
    auto textLeaf = [&](NodeId parent, const TextStyle& style, std::string_view text) {
        return leaf(parent, measureLabel(ctx, style, text));
    };
    auto group = [&](NodeId parent, Axis axis, float gap) {
        NodeStyle st;
        st.axis = axis;
        st.gap = gap;
        return t.add(parent, st);
    };

    // Header.
    NodeId header = group(root, Axis::Row, 14);
    t.style(header).crossAlign = Align::Center;
    const NodeId logoNode = leaf(header, {42, 36});
    NodeId titleCol = group(header, Axis::Column, 4);
    const NodeId title = textLeaf(titleCol, text::kHeading, "ZODAR");
    const NodeId subtitle = textLeaf(titleCol, text::kOverline, "UI Sandbox · Stage 1 · temporary SDL backend");

    // Colour tokens.
    NodeId colorsSec = group(root, Axis::Column, 14);
    const NodeId colorsHead = textLeaf(colorsSec, text::kLabel, "COLOUR TOKENS");
    NodeStyle wrapRow;
    wrapRow.axis = Axis::Row;
    wrapRow.wrap = true;
    wrapRow.gap = 16;
    wrapRow.lineGap = 16;
    wrapRow.widthSizing = Sizing::Fixed;
    wrapRow.size = {logical.width - 96, 0};
    const NodeId swatchRow = t.add(colorsSec, wrapRow);
    std::vector<NodeId> swatches;
    for (size_t i = 0; i < colorTokens().size(); ++i) swatches.push_back(leaf(swatchRow, {196, 104}));

    // Typography.
    NodeId typeSec = group(root, Axis::Column, 14);
    const NodeId typeHead = textLeaf(typeSec, text::kLabel, "TYPOGRAPHY");
    struct TypeRow { NodeId sample, meta; const TextStyle* style; std::string sampleText, metaText; };
    std::vector<TypeRow> typeRows;
    for (const TextStyle& st : textStyles()) {
        NodeId row = group(typeSec, Axis::Row, 24);
        t.style(row).crossAlign = Align::Center;
        std::string sample = st.name == std::string_view("UI/Path") ? "D:/ZODAR/Projects/Station17" : "Recent Projects 0123";
        std::string meta = std::string(st.name) + "  ·  " + std::string(fontFaceName(st.face)) + "  ·  " +
                           fmt("%.0f px", st.size) + " / " + fmt("%.2f", st.lineHeight) + " / " +
                           fmt("%.0f%%", st.letterSpacing * 100);
        NodeStyle metaCol;
        metaCol.widthSizing = Sizing::Fixed;
        metaCol.size = {380, 0};
        const NodeId metaBox = t.add(row, metaCol);
        const NodeId metaNode = textLeaf(metaBox, text::kCaption, meta);
        const NodeId sampleNode = textLeaf(row, st, sample);
        typeRows.push_back({sampleNode, metaNode, &st, sample, meta});
    }

    // Buttons.
    NodeId btnSec = group(root, Axis::Column, 14);
    const NodeId btnHead = textLeaf(btnSec, text::kLabel, "BUTTON");
    const char* states[] = {"Default", "Hover", "Pressed", "Disabled", "Live"};
    const char* variants[] = {"Primary", "Secondary", "Ghost"};
    NodeId stateRow = group(btnSec, Axis::Row, 16);
    leaf(stateRow, {110, 16});
    std::vector<NodeId> stateLabels;
    for (const char* st : states) {
        NodeStyle cell;
        cell.widthSizing = Sizing::Fixed;
        cell.size = {196, 0};
        const NodeId c = t.add(stateRow, cell);
        stateLabels.push_back(textLeaf(c, text::kOverline, st));
    }
    std::vector<NodeId> variantLabels;
    std::vector<std::vector<NodeId>> buttonCells(3);
    for (int v = 0; v < 3; ++v) {
        NodeId row = group(btnSec, Axis::Row, 16);
        t.style(row).crossAlign = Align::Center;
        NodeStyle lab;
        lab.widthSizing = Sizing::Fixed;
        lab.size = {110, 0};
        const NodeId lb = t.add(row, lab);
        variantLabels.push_back(textLeaf(lb, text::kCaption, variants[v]));
        for (int c = 0; c < 5; ++c) buttonCells[v].push_back(leaf(row, {196, metric::kControlHeight}));
    }

    // Logo.
    NodeId logoSec = group(root, Axis::Column, 14);
    const NodeId logoHead = textLeaf(logoSec, text::kLabel, "ORIGINAL LOGO (SVG, RASTERIZED AT CURRENT SCALE)");
    NodeId logoRow = group(logoSec, Axis::Row, 48);
    t.style(logoRow).crossAlign = Align::Center;
    const NodeId lockupNode = leaf(logoRow, {225, 145});
    const NodeId iconNode = leaf(logoRow, {112, 96});

    const NodeId footerNode = textLeaf(root, text::kCaption, footer);

    t.compute(logical, {0, -scroll});

    auto at = [&](NodeId id) { return t.rect(id); };
    auto lbl = [&](NodeId id, const TextStyle& st, Color c, std::string_view txt) {
        label(ctx, {at(id).x, at(id).y}, st, c, txt);
    };

    image(ctx, at(logoNode), logos.iconSmall);
    lbl(title, text::kHeading, color::kBrandPrimary, "ZODAR");
    lbl(subtitle, text::kOverline, color::kBrandAccent, "UI Sandbox · Stage 1 · temporary SDL backend");

    lbl(colorsHead, text::kLabel, color::kTextSecondary, "COLOUR TOKENS");
    for (size_t i = 0; i < swatches.size(); ++i) {
        const ColorToken& tok = colorTokens()[i];
        const Rect r = at(swatches[i]);
        const Rect chip{r.x, r.y, r.width, 56};
        dl.fillRect(ctx.toPixels(chip), tok.value);
        dl.strokeRect(ctx.toPixels(chip), ctx.px(1), color::kBorder);
        label(ctx, {r.x, r.y + 64}, text::kCaption, color::kBrandPrimary, tok.name);
        label(ctx, {r.x, r.y + 82}, text::kPath, color::kTextSecondary,
              hex(tok.value) + (tok.official ? "  official" : "  UI addition"));
        const Rect px = ctx.toPixels(chip);
        probes.push_back({std::string(tok.name), {px.x + px.width * 0.5f, px.y + px.height * 0.5f}, tok.value});
    }

    lbl(typeHead, text::kLabel, color::kTextSecondary, "TYPOGRAPHY");
    for (const TypeRow& row : typeRows) {
        lbl(row.meta, text::kCaption, color::kTextMuted, row.metaText);
        lbl(row.sample, *row.style, color::kBrandPrimary, row.sampleText);
    }

    lbl(btnHead, text::kLabel, color::kTextSecondary, "BUTTON");
    for (size_t i = 0; i < stateLabels.size(); ++i) lbl(stateLabels[i], text::kOverline, color::kTextMuted, states[i]);
    const WidgetState forced[] = {WidgetState::Default, WidgetState::Hover, WidgetState::Pressed, WidgetState::Disabled};
    for (int v = 0; v < 3; ++v) {
        lbl(variantLabels[static_cast<size_t>(v)], text::kCaption, color::kTextSecondary, variants[v]);
        for (int c = 0; c < 5; ++c) {
            const Rect cell = at(buttonCells[static_cast<size_t>(v)][static_cast<size_t>(c)]);
            const std::string labelText = v == 0 ? "Create Project" : (v == 1 ? "Open Project" : "Cancel");
            const Size2 natural = measureButton(ctx, labelText);
            const Rect r{cell.x, cell.y, natural.width, natural.height};
            const std::string key = "gallery." + std::to_string(v) + "." + std::to_string(c);
            const bool disabled = c == 3;
            std::optional<WidgetState> force;
            if (c < 4) force = forced[c];
            button(ctx, widgetId(key), r, static_cast<ButtonVariant>(v), labelText, disabled, force);
            if (v == 0 && c == 0) {
                const Rect px = ctx.toPixels(r);
                probes.push_back({"button.primary.fill", {px.x + 4, px.y + px.height * 0.5f}, color::kBrandAccent});
            }
            if (v == 0 && c == 1) {
                const Rect px = ctx.toPixels(r);
                probes.push_back({"button.primary.hover", {px.x + 4, px.y + px.height * 0.5f}, color::kAccentHover});
            }
        }
    }

    lbl(logoHead, text::kLabel, color::kTextSecondary, "ORIGINAL LOGO (SVG, RASTERIZED AT CURRENT SCALE)");
    image(ctx, at(lockupNode), logos.lockupTex);
    image(ctx, at(iconNode), logos.iconLarge);

    lbl(footerNode, text::kCaption, color::kTextMuted, footer);
    probes.push_back({"background", {2, 2}, color::kBrandBackground});
}

bool readPixels(SDL_Renderer* renderer, std::vector<uint8_t>& rgba, int& w, int& h) {
    SDL_Surface* raw = SDL_RenderReadPixels(renderer, nullptr);
    if (!raw) return false;
    SDL_Surface* s = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    if (!s) return false;
    w = s->w;
    h = s->h;
    rgba.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4, static_cast<uint8_t*>(s->pixels) + static_cast<size_t>(y) * s->pitch,
                    static_cast<size_t>(w) * 4);
    }
    SDL_DestroySurface(s);
    return true;
}

int run(const Options& opt) {
    if (!SDL_Init(SDL_INIT_VIDEO)) fail(std::string("SDL_Init failed: ") + SDL_GetError());

    const float initialScale = opt.displayScale.value_or(1.0f) * opt.userScale;
    int w = opt.size ? opt.size->first : static_cast<int>(std::lround(1280 * initialScale));
    int h = opt.size ? opt.size->second : static_cast<int>(std::lround(800 * initialScale));
    // Pixel size is chosen explicitly; no OS high-density mapping so --size is exact.
    SDL_Window* window = SDL_CreateWindow("ZODAR UI Sandbox (temporary)", w, h, SDL_WINDOW_RESIZABLE);
    if (!window) fail(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    SDL_Renderer* renderer = SDL_CreateRenderer(window, opt.software ? "software" : nullptr);
    if (!renderer) fail(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    SDL_SetRenderVSync(renderer, 1);
    ZODAR_LOG_INFO("UI sandbox: SDL renderer '{}', video driver '{}'", SDL_GetRendererName(renderer),
                   SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");

    const std::filesystem::path res = resourceDir(opt);
    TextureStore textures;
    FontLibrary fonts(res, textures);
    Logos logos{SvgDocument::load(res / "Logo/LOGO_NoBackGround/White/SVG/512 X 512 LOGO_NoBackGround.svg"),
                SvgDocument::load(res / "Logo/LOGO_TEXT_NoBackGroundBlack/White/SVG/512 X 512 LOGO_TEXT_NoBackGroundBlack.svg")};
    Context ctx(fonts);
    InputState input;
    DrawList list;
    sandbox::SdlRendererBackend backend(renderer);
    std::vector<Probe> probes;
    float scroll = 0.0f;
    int exitCode = 0;

    for (int frame = 1;; ++frame) {
        input.beginFrame();
        bool quit = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) quit = true;
            SDL_ConvertEventToRenderCoordinates(renderer, &e);
            if (auto ev = sandbox::translateEvent(e)) input.apply(*ev);
        }
        if (quit) break;

        int pw = 0, ph = 0;
        SDL_GetRenderOutputSize(renderer, &pw, &ph);
        const float display = opt.displayScale.value_or(SDL_GetWindowDisplayScale(window));
        const Size2 pixels{static_cast<float>(pw), static_cast<float>(ph)};
        const float scale = computeScale(ScalePolicy::Density, {pixels, display, opt.userScale});
        const Size2 logical = logicalSize(pixels, scale);
        scroll = std::max(0.0f, scroll - input.wheel().y * 48.0f);

        logos.ensure(textures, scale);
        const std::string footer = "Scale " + fmt("%.2f", scale) + "  ·  display " + fmt("%.2f", display) +
                                   "  ·  user " + fmt("%.2f", opt.userScale) + "  ·  window " + std::to_string(pw) + "×" +
                                   std::to_string(ph) + " px  ·  logical " + fmt("%.0f", logical.width) + "×" +
                                   fmt("%.0f", logical.height) + "  ·  renderer: " + SDL_GetRendererName(renderer);
        list.reset({0, 0, pixels.width, pixels.height});
        ctx.beginFrame(list, input, scale);
        buildGallery(ctx, logos, logical, scroll, footer, probes);
        ctx.endFrame();

        backend.sync(textures);
        SDL_SetRenderDrawColor(renderer, 0x11, 0x11, 0x11, 0xFF);
        SDL_RenderClear(renderer);
        backend.render(list);

        if (opt.frames > 0 && frame >= opt.frames) {
            std::vector<uint8_t> rgba;
            int rw = 0, rh = 0;
            if ((opt.check || !opt.screenshot.empty()) && !readPixels(renderer, rgba, rw, rh)) {
                ZODAR_LOG_ERROR("SDL_RenderReadPixels failed: {}", SDL_GetError());
                exitCode = 3;
            }
            if (!rgba.empty() && !opt.screenshot.empty()) {
                if (!stbi_write_png(opt.screenshot.c_str(), rw, rh, 4, rgba.data(), rw * 4)) exitCode = 3;
                else ZODAR_LOG_INFO("Screenshot written: {} ({}x{})", opt.screenshot, rw, rh);
            }
            if (opt.check && !rgba.empty()) {
                int failures = 0;
                for (const Probe& p : probes) {
                    const int x = static_cast<int>(p.pixel.x), y = static_cast<int>(p.pixel.y);
                    if (x < 0 || y < 0 || x >= rw || y >= rh) continue; // Scrolled out of view.
                    const uint8_t* px = rgba.data() + (static_cast<size_t>(y) * rw + x) * 4;
                    const Color got{px[0], px[1], px[2], 255};
                    if (got != p.expected) {
                        ZODAR_LOG_ERROR("check '{}' at ({}, {}): expected {} got {}", p.name, x, y, hex(p.expected), hex(got));
                        ++failures;
                    }
                }
                ZODAR_LOG_INFO("Pixel checks: {} probes, {} failures", probes.size(), failures);
                if (failures > 0) exitCode = 4;
            }
            SDL_RenderPresent(renderer);
            break;
        }
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}

} // namespace

int main(int argc, char** argv) {
    log::init({spdlog::level::info, ""});
    int code = 1;
    try {
        code = run(parse(argc, argv));
    } catch (const std::exception& e) {
        ZODAR_LOG_ERROR("Fatal: {}", e.what());
        code = 1;
    }
    log::shutdown();
    return code;
}
