#include "UI/Widgets/Button.h"

#include <cmath>

namespace zodar::ui {
namespace {

constexpr Color kNone{0, 0, 0, 0};

Color scaleRgb(Color c, float f) {
    auto ch = [f](uint8_t v) { return static_cast<uint8_t>(std::lround(static_cast<float>(v) * f)); };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

} // namespace

ButtonColors buttonColors(ButtonVariant variant, WidgetState state) {
    ButtonColors c;
    switch (variant) {
    case ButtonVariant::Primary:
        c = {color::kBrandAccent, kNone, color::kBrandBackground};
        if (state == WidgetState::Hover) c.fill = color::kAccentHover;
        if (state == WidgetState::Pressed) c.fill = scaleRgb(color::kBrandAccent, 0.88f); // Unconfirmed.
        break;
    case ButtonVariant::Secondary:
        c = {color::kSurfaceRaised, color::kBorder, color::kBrandPrimary};
        if (state == WidgetState::Hover) c.border = color::kTextMuted;
        if (state == WidgetState::Pressed) {
            c.fill = color::kBrandSurface; // Unconfirmed.
            c.border = color::kTextMuted;
        }
        break;
    case ButtonVariant::Ghost:
        c = {kNone, kNone, color::kTextSecondary};
        if (state == WidgetState::Hover || state == WidgetState::Pressed) c.text = color::kBrandPrimary;
        if (state == WidgetState::Pressed) c.fill = color::kSurfaceRaised; // Unconfirmed.
        break;
    }
    if (state == WidgetState::Disabled) {
        c.fill = c.fill.withAlpha(kButtonDisabledOpacity);
        c.border = c.border.withAlpha(kButtonDisabledOpacity);
        c.text = c.text.withAlpha(kButtonDisabledOpacity);
    }
    return c;
}

Size2 measureButton(Context& ctx, std::string_view label) {
    const Size2 text = ctx.fonts().measure(text::kButton, ctx.scale(), label);
    return {std::ceil(text.width / ctx.scale()) + 2.0f * kButtonPaddingX, metric::kControlHeight};
}

Interaction button(Context& ctx, WidgetId id, Rect logical, ButtonVariant variant, std::string_view label,
                   bool disabled, std::optional<WidgetState> forceState) {
    Interaction in = ctx.interact(id, logical, disabled);
    if (forceState) {
        in.state = *forceState;
        in.clicked = false;
    }
    const ButtonColors colors = buttonColors(variant, in.state);
    const Rect r = ctx.toPixels(logical);
    const float radius = metric::kRadius * ctx.scale();
    DrawList& dl = ctx.draw();
    dl.fillRoundRect(r, radius, colors.fill);
    dl.strokeRoundRect(r, radius, ctx.px(metric::kBorderWidth), colors.border);

    const Size2 text = ctx.fonts().measure(text::kButton, ctx.scale(), label);
    const Vec2 pos{std::round(r.x + (r.width - text.width) * 0.5f), std::round(r.y + (r.height - text.height) * 0.5f)};
    ctx.fonts().draw(dl, pos, text::kButton, ctx.scale(), colors.text, label);
    return in;
}

} // namespace zodar::ui
