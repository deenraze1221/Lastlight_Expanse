#include "UI/Screens/LoadingScreen.h"

#include "UI/Scale.h"
#include "UI/Widgets/Button.h"
#include "UI/Widgets/Image.h"
#include "UI/Widgets/Label.h"
#include "UI/Widgets/ProgressBar.h"

#include <algorithm>
#include <cmath>

namespace zodar::ui {
namespace {

constexpr float kContentWidth = kLoadingDesignSize.width - 2.0f * kLoadingMarginX; // 832
constexpr float kStatusTextHeight = 17.0f;  // 13 px x 1.3, rounded.
constexpr float kHintHeight = 16.0f;        // 12 px x 1.3, rounded.
constexpr float kVersionHeight = 14.0f;     // 11 px x 1.3, rounded.
constexpr float kTrackHeight = 2.0f;
constexpr float kRowToTrackGap = 12.0f;
constexpr float kHintGap = 8.0f;
constexpr float kButtonGap = 10.0f;
constexpr float kLogTop = 40.0f;
constexpr float kLogGapAboveTrack = 16.0f;

float textY(Rect row, float textHeight) {
    return row.y + std::round((row.height - textHeight) * 0.5f);
}

} // namespace

LoadingLayout computeLoadingLayout(const StartupSnapshot& s, bool logOpen, Size2 openLogButton, Size2 retryButton) {
    LoadingLayout l;
    const bool failed = s.state == StartupState::Failed;
    l.showHint = showFirstLaunchHint(s);
    l.showActions = failed;

    // The status block grows upward from a fixed bottom edge.
    float bottom = kLoadingStatusBottom;
    if (l.showHint) {
        l.hint = {kLoadingMarginX, bottom - kHintHeight, kContentWidth, kHintHeight};
        bottom -= kHintHeight + kHintGap;
    }
    const float rowHeight = failed ? std::max(kStatusTextHeight, std::max(openLogButton.height, retryButton.height))
                                   : kStatusTextHeight;
    l.statusRow = {kLoadingMarginX, bottom - rowHeight, kContentWidth, rowHeight};
    bottom = l.statusRow.y - kRowToTrackGap;
    l.track = {kLoadingMarginX, bottom - kTrackHeight, kContentWidth, kTrackHeight};

    if (failed) {
        const float right = kLoadingMarginX + kContentWidth;
        l.retry = {right - retryButton.width, l.statusRow.y + (rowHeight - retryButton.height) * 0.5f, retryButton.width,
                   retryButton.height};
        l.openLog = {l.retry.x - kButtonGap - openLogButton.width, l.statusRow.y + (rowHeight - openLogButton.height) * 0.5f,
                     openLogButton.width, openLogButton.height};
    }

    l.version = {kLoadingMarginX, kLoadingVersionTop, kContentWidth * 0.75f, kVersionHeight};
    l.percent = {kLoadingMarginX + kContentWidth * 0.75f, kLoadingVersionTop, kContentWidth * 0.25f, kVersionHeight};

    l.showLog = logOpen;
    l.logPanel = {kLoadingMarginX, kLogTop, kContentWidth, std::max(0.0f, l.track.y - kLogGapAboveTrack - kLogTop)};
    return l;
}

std::string engineVersionLabel() {
    std::string v = "ZODAR Engine v" + std::to_string(ZODAR_VERSION_MAJOR) + "." + std::to_string(ZODAR_VERSION_MINOR);
    if (ZODAR_VERSION_PATCH != 0) {
        v += "." + std::to_string(ZODAR_VERSION_PATCH);
    }
    return v;
}

LoadingScreen::LoadingScreen(const std::filesystem::path& dir)
    : m_artwork(SvgDocument::load(dir / "Logo/Loading/Loading.svg")) {}

float LoadingScreen::scaleFor(Size2 windowPixels) {
    return computeScale(ScalePolicy::FitDesign, {windowPixels, 1.0f, 1.0f, kLoadingDesignSize});
}

bool LoadingScreen::keepBackgroundShape(const SvgShapeInfo& shape) {
    const Rect& b = shape.bounds;
    const bool insideCppTag = b.x >= kRemovedCppTag.x && b.y >= kRemovedCppTag.y && b.right() <= kRemovedCppTag.right() &&
                              b.bottom() <= kRemovedCppTag.bottom();
    return !insideCppTag;
}

void LoadingScreen::ensureBackground(TextureStore& textures, float scale) {
    if (m_background != kNoTexture && m_backgroundScale == scale) {
        return;
    }
    Image img = m_artwork.rasterize(scale, std::nullopt, &LoadingScreen::keepBackgroundShape);
    if (m_background == kNoTexture) {
        m_background = textures.add(std::move(img));
    } else {
        textures.update(m_background, std::move(img));
    }
    m_backgroundScale = scale;
}

void LoadingScreen::drawLog(Context& ctx, Rect panel, const LogBuffer* log) {
    DrawList& dl = ctx.draw();
    const Rect px = ctx.toPixels(panel);
    dl.fillRect(px, color::kBrandSurface);
    dl.strokeRect(px, ctx.px(metric::kBorderWidth), color::kBorder);

    const float pad = 12.0f;
    label(ctx, {panel.x + pad, panel.y + 10.0f}, text::kOverline, color::kTextSecondary, "Log");
    const float headerBottom = panel.y + 34.0f;
    dl.fillRect(ctx.toPixels({panel.x, headerBottom, panel.width, metric::kBorderWidth}), color::kBorder);

    const Rect body{panel.x + pad, headerBottom + 8.0f, panel.width - 2.0f * pad, panel.bottom() - headerBottom - 16.0f};
    dl.pushClip(ctx.toPixels(body));
    const float lineHeight = std::round(text::kPath.size * text::kPath.lineHeight);
    if (!log || log->lines().empty()) {
        label(ctx, {body.x, body.y}, text::kPath, color::kTextMuted, "No log output yet.");
    } else {
        // Newest lines at the bottom; older lines scroll off the top.
        const auto& lines = log->lines();
        const size_t visible = static_cast<size_t>(std::max(1.0f, std::floor(body.height / lineHeight)));
        const size_t first = lines.size() > visible ? lines.size() - visible : 0;
        float y = body.y;
        for (size_t i = first; i < lines.size(); ++i, y += lineHeight) {
            const LogLine& line = lines[i];
            const Color c = line.level == LogLevel::Error     ? color::kBrandError
                            : line.level == LogLevel::Warning ? color::kBrandPrimary
                                                              : color::kTextSecondary;
            label(ctx, {body.x, y}, text::kPath, c, formatLogLine(line));
        }
    }
    dl.popClip();
}

LoadingActions LoadingScreen::draw(Context& ctx, TextureStore& textures, const StartupSnapshot& s, const LogBuffer* log,
                                   double t, float dt, std::string_view version) {
    LoadingActions actions;
    DrawList& dl = ctx.draw();
    const float scale = ctx.scale();

    // Background colour, then the original artwork (pattern + logo, C++ tag removed).
    dl.fillRect({0, 0, 1.0e6f, 1.0e6f}, color::kBrandBackground);
    ensureBackground(textures, scale);
    image(ctx, {0, 0, kLoadingDesignSize.width, kLoadingDesignSize.height}, m_background);

    const bool failed = s.state == StartupState::Failed;
    const bool exact = hasExactProgress(s);
    const std::string openLabel = m_logOpen ? "Hide log" : "Open log";
    const LoadingLayout l = computeLoadingLayout(s, m_logOpen, measureButton(ctx, openLabel), measureButton(ctx, "Retry"));

    // Progress: smooth only toward values the source actually reported.
    if (exact) {
        m_displayed = smoothProgress(m_displayed, *s.overall, dt);
    } else if (s.state == StartupState::Completed) {
        m_displayed = 1.0f; // Finished is a fact, not an estimate.
    }
    std::optional<float> fraction;
    if (exact || s.state == StartupState::Completed) fraction = m_displayed;
    if (failed && !exact) fraction = 1.0f; // A stopped, red bar instead of a moving animation.
    progressBar(ctx, l.track, fraction, t, failed ? color::kBrandError : color::kBrandAccent);

    // Status row.
    const std::string stage(stageLabel(s.stage));
    if (failed) {
        const std::string msg = stage + " failed";
        label(ctx, {l.statusRow.x, textY(l.statusRow, kStatusTextHeight)}, text::kLoadingStatus, color::kBrandPrimary, msg);
        if (button(ctx, widgetId("loading.openLog"), l.openLog, ButtonVariant::Secondary, openLabel).clicked) {
            m_logOpen = !m_logOpen;
            actions.toggledLog = true;
        }
        if (button(ctx, widgetId("loading.retry"), l.retry, ButtonVariant::Primary, "Retry").clicked) {
            actions.retry = true;
            m_logOpen = false;
        }
    } else {
        label(ctx, {l.statusRow.x, l.statusRow.y}, text::kLoadingStatus, color::kTextSecondary, stage);
        const std::string counts = formatCounts(s.counts);
        if (!counts.empty()) {
            const Size2 w = measureLabel(ctx, text::kLoadingDetail, counts);
            label(ctx, {l.statusRow.right() - w.width, l.statusRow.y}, text::kLoadingDetail, color::kTextMuted, counts);
        }
    }
    if (l.showHint) {
        label(ctx, {l.hint.x, l.hint.y}, text::kLoadingHint, color::kBrandAccent, kFirstLaunchHint);
    }

    // Version (with the approved muted C++ label) and, only with exact progress, the percentage.
    label(ctx, {l.version.x, l.version.y}, text::kLoadingVersion, color::kTextMuted,
          std::string(version) + "  \xC2\xB7  C++");
    if (exact && !failed) {
        const std::string pct = std::to_string(static_cast<int>(std::floor(m_displayed * 100.0f + 1e-4f))) + "%";
        const Size2 w = measureLabel(ctx, text::kLoadingVersion, pct);
        label(ctx, {l.percent.right() - w.width, l.percent.y}, text::kLoadingVersion, color::kTextMuted, pct);
    }

    if (l.showLog) {
        drawLog(ctx, l.logPanel, log);
    }
    return actions;
}

} // namespace zodar::ui
