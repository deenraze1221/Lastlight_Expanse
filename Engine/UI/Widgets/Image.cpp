#include "UI/Widgets/Image.h"

namespace zodar::ui {

void image(Context& ctx, Rect logical, TextureId texture, Color tint) {
    ctx.draw().image(ctx.toPixels(logical), texture, tint);
}

} // namespace zodar::ui
