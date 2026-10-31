#pragma once

#include "UI/Context.h"

namespace zodar::ui {

// Draws a texture (e.g. a rasterized SVG) into a design-unit rectangle, snapped to pixels.
void image(Context& ctx, Rect logical, TextureId texture, Color tint = color::kBrandPrimary);

} // namespace zodar::ui
