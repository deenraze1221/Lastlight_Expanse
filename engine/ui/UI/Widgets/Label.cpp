#include "UI/Widgets/Label.h"

namespace zodar::ui {

Size2 measureLabel(Context& ctx, const TextStyle& style, std::string_view text) {
    const Size2 px = ctx.fonts().measure(style, ctx.scale(), text);
    return {px.width / ctx.scale(), px.height / ctx.scale()};
}

void label(Context& ctx, Vec2 topLeft, const TextStyle& style, Color color, std::string_view text) {
    ctx.fonts().draw(ctx.draw(), {topLeft.x * ctx.scale(), topLeft.y * ctx.scale()}, style, ctx.scale(), color, text);
}

} // namespace zodar::ui
