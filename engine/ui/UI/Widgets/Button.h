#pragma once

#include "UI/Context.h"

#include <optional>

namespace zodar::ui {

// Figma component "Button": Primary = the one main action on a screen (green),
// Secondary = other actions, Ghost = low emphasis.
enum class ButtonVariant : uint8_t { Primary, Secondary, Ghost };

struct ButtonColors {
    Color fill;   // Alpha 0 = no fill.
    Color border; // Alpha 0 = no border.
    Color text;
};
ButtonColors buttonColors(ButtonVariant variant, WidgetState state);

inline constexpr float kButtonPaddingX = 20.0f;
inline constexpr float kButtonDisabledOpacity = 0.35f;

// Natural size in design units (height = metric::kControlHeight).
Size2 measureButton(Context& ctx, std::string_view label);

// Draws and handles a button. `forceState` renders a fixed state (component gallery, tests).
Interaction button(Context& ctx, WidgetId id, Rect logical, ButtonVariant variant, std::string_view label,
                   bool disabled = false, std::optional<WidgetState> forceState = std::nullopt);

} // namespace zodar::ui
