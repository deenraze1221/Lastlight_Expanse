#include "UI/Context.h"

#include "Core/Error.h"

#include <cmath>

namespace zodar::ui {

WidgetId widgetId(std::string_view key) {
    uint64_t h = 1469598103934665603ull;
    for (char c : key) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h == 0 ? 1 : h;
}

void Context::beginFrame(DrawList& list, const InputState& input, float scale) {
    ZODAR_ENSURE(scale > 0.0f, "UI scale must be positive");
    m_draw = &list;
    m_input = &input;
    m_scale = scale;
}

void Context::endFrame() {
    if (m_input && !m_input->isDown(PointerButton::Left)) {
        m_active = 0;
    }
}

float Context::px(float logical) const {
    if (logical <= 0.0f) {
        return 0.0f;
    }
    return std::max(1.0f, std::round(logical * m_scale));
}

Interaction Context::interact(WidgetId id, Rect logical, bool disabled) {
    if (disabled) {
        return {WidgetState::Disabled, false};
    }
    const bool over = m_input->pointerInside() && toPixels(logical).contains(m_input->pointer());
    if (over && m_input->wasPressed(PointerButton::Left)) {
        m_active = id;
    }
    Interaction result;
    const bool active = m_active == id;
    if (active && m_input->wasReleased(PointerButton::Left)) {
        result.clicked = over;
        m_active = 0;
    }
    if (active && m_input->isDown(PointerButton::Left)) {
        result.state = over ? WidgetState::Pressed : WidgetState::Hover;
    } else if (over && (m_active == 0 || active)) {
        result.state = WidgetState::Hover;
    }
    return result;
}

} // namespace zodar::ui
