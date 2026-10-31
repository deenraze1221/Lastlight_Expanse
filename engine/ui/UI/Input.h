#pragma once

#include "UI/Geometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zodar::ui {

// The UI's own input vocabulary. Platform code (or the sandbox) translates OS events into
// these; the UI module never sees SDL types. Positions are physical pixels.
enum class PointerButton : uint8_t { Left, Right, Middle };
enum class Key : uint8_t { Unknown, Tab, Enter, Escape, Backspace, Delete, Left, Right, Up, Down, Home, End, Space };

struct InputEvent {
    enum class Type : uint8_t { PointerMove, PointerDown, PointerUp, PointerLeave, Wheel, KeyDown, KeyUp, Text };
    Type type = Type::PointerMove;
    Vec2 position;
    PointerButton button = PointerButton::Left;
    Vec2 wheel;
    Key key = Key::Unknown;
    bool shift = false;
    bool ctrl = false;
    std::string text; // UTF-8 for Type::Text.
};

// Per-frame input state built from events.
class InputState {
public:
    void beginFrame();          // Clears per-frame edges (pressed/released, keys, text).
    void apply(const InputEvent& event);

    Vec2 pointer() const { return m_pointer; }
    bool pointerInside() const { return m_inside; }
    bool isDown(PointerButton b) const { return m_down[index(b)]; }
    bool wasPressed(PointerButton b) const { return m_pressed[index(b)]; }
    bool wasReleased(PointerButton b) const { return m_released[index(b)]; }
    Vec2 wheel() const { return m_wheel; }
    const std::vector<Key>& keysPressed() const { return m_keys; }
    const std::string& text() const { return m_text; }

private:
    static size_t index(PointerButton b) { return static_cast<size_t>(b); }

    Vec2 m_pointer{-1.0f, -1.0f};
    bool m_inside = false;
    bool m_down[3]{};
    bool m_pressed[3]{};
    bool m_released[3]{};
    Vec2 m_wheel;
    std::vector<Key> m_keys;
    std::string m_text;
};

} // namespace zodar::ui
