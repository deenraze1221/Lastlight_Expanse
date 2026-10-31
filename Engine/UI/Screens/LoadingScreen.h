#pragma once

#include "UI/Context.h"
#include "UI/LogBuffer.h"
#include "UI/Startup.h"
#include "UI/Svg.h"
#include "UI/WindowPlan.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace zodar::ui {

// Loading-window text styles (approved faces; sizes and spacing from the approved prototype,
// not yet confirmed as final measurements, see Docs/UI_SPEC.md).
namespace text {
inline constexpr TextStyle kLoadingStatus{"Loading/Status", FontFace::RajdhaniRegular, 13.0f, 1.3f, 0.24f, true};
inline constexpr TextStyle kLoadingDetail{"Loading/Detail", FontFace::RajdhaniRegular, 13.0f, 1.3f, 0.14f, false};
inline constexpr TextStyle kLoadingHint{"Loading/Hint", FontFace::RajdhaniRegular, 12.0f, 1.3f, 0.14f, false};
inline constexpr TextStyle kLoadingVersion{"Loading/Version", FontFace::RajdhaniRegular, 11.0f, 1.3f, 0.12f, false};
} // namespace text

// Positions of the loading window's parts on the 960x540 design canvas.
struct LoadingLayout {
    Rect track;          // Progress bar.
    Rect statusRow;      // Stage label (left) + real counts (right), or failure text + buttons.
    bool showHint = false;
    Rect hint;           // First-launch hint (Preparing shaders > 30 s).
    bool showActions = false;
    Rect openLog;        // "Open log" / "Hide log" (failure only).
    Rect retry;          // "Retry" (failure only).
    Rect version;        // "ZODAR Engine v0.1 · C++" (bottom left).
    Rect percent;        // Overall percentage (bottom right), only with exact progress.
    bool showLog = false;
    Rect logPanel;       // Log viewer above the status area.
};

inline constexpr float kLoadingMarginX = 64.0f;
inline constexpr float kLoadingStatusBottom = 496.0f; // Bottom edge of the status block.
inline constexpr float kLoadingVersionTop = 508.0f;

// Pure layout (design units). Button sizes come from measureButton().
LoadingLayout computeLoadingLayout(const StartupSnapshot& snapshot, bool logOpen, Size2 openLogButton,
                                   Size2 retryButton);

// Real engine version from the build ("ZODAR Engine v0.1"; patch shown when non-zero).
std::string engineVersionLabel();

struct LoadingActions {
    bool retry = false;     // User pressed Retry (failure state).
    bool toggledLog = false;
};

// The startup loading window (approved design: original Loading.svg artwork without the C++
// tag, thin green progress bar, stage text, real counts only, first-launch hint, Open log and
// Retry on failure, version with C++ bottom left). Renderer-independent: draws into a Context.
class LoadingScreen {
public:
    // Loads the original Resources/UI/Logo/Loading/Loading.svg.
    explicit LoadingScreen(const std::filesystem::path& uiResourceDir);

    // UI scale for a loading window of `windowPixels` (fits the 960x540 design).
    static float scaleFor(Size2 windowPixels);
    // Background shapes kept from Loading.svg: everything except the "C++" tag (removed by spec).
    static bool keepBackgroundShape(const SvgShapeInfo& shape);
    static constexpr Rect kRemovedCppTag{870.0f, 25.0f, 60.0f, 25.0f}; // Design units.

    // Draws one frame. `ctx` must have been started with scaleFor(window size).
    LoadingActions draw(Context& ctx, TextureStore& textures, const StartupSnapshot& snapshot, const LogBuffer* log,
                        double timeSeconds, float deltaSeconds, std::string_view version);

    bool logOpen() const { return m_logOpen; }
    void setLogOpen(bool open) { m_logOpen = open; }
    float displayedProgress() const { return m_displayed; }
    const SvgDocument& artwork() const { return m_artwork; }

private:
    void ensureBackground(TextureStore& textures, float scale);
    void drawLog(Context& ctx, Rect panel, const LogBuffer* log);

    SvgDocument m_artwork;
    TextureId m_background = kNoTexture;
    float m_backgroundScale = 0.0f;
    bool m_logOpen = false;
    float m_displayed = 0.0f;
};

} // namespace zodar::ui
