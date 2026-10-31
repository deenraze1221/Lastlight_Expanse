#pragma once

#include "UI/Context.h"

namespace zodar::ui {

// Size of a single-line label in design units.
Size2 measureLabel(Context& ctx, const TextStyle& style, std::string_view text);
// Draws a single-line label whose line box starts at `topLeft` (design units).
void label(Context& ctx, Vec2 topLeft, const TextStyle& style, Color color, std::string_view text);

} // namespace zodar::ui
