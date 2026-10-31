#pragma once

#include "UI/DrawList.h"
#include "UI/Font.h"
#include "UI/Input.h"

#include <cstdint>
#include <string_view>

namespace zodar::ui {

using WidgetId = uint64_t;
// Stable id from a string (FNV-1a), e.g. widgetId("launcher.create").
WidgetId widgetId(std::string_view key);

enum class WidgetState : uint8_t { Default, Hover, Pressed, Disabled };

struct Interaction {
    WidgetState state = WidgetState::Default;
    bool clicked = false;
};

// Per-frame UI context: where widgets draw, which input they read and the UI scale.
// Widgets take design-unit rectangles; the context converts them to pixels.
class Context {
public:
    explicit Context(FontLibrary& fonts) : m_fonts(fonts) {}

    void beginFrame(DrawList& list, const InputState& input, float scale);
    void endFrame();

    DrawList& draw() { return *m_draw; }
    FontLibrary& fonts() { return m_fonts; }
    const InputState& input() const { return *m_input; }
    float scale() const { return m_scale; }

    Rect toPixels(Rect logical) const { return logical.scaled(m_scale).snapped(); }
    float px(float logical) const;                // Rounded to whole pixels, at least 1 for > 0.

    // Button-like hit testing with press capture: a click needs press and release on the widget.
    Interaction interact(WidgetId id, Rect logical, bool disabled);

private:
    FontLibrary& m_fonts;
    DrawList* m_draw = nullptr;
    const InputState* m_input = nullptr;
    float m_scale = 1.0f;
    WidgetId m_active = 0;
};

} // namespace zodar::ui
