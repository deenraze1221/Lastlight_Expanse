#include "UI/Input.h"

namespace zodar::ui {

void InputState::beginFrame() {
    for (size_t i = 0; i < 3; ++i) {
        m_pressed[i] = false;
        m_released[i] = false;
    }
    m_wheel = {};
    m_keys.clear();
    m_text.clear();
}

void InputState::apply(const InputEvent& e) {
    switch (e.type) {
    case InputEvent::Type::PointerMove:
        m_pointer = e.position;
        m_inside = true;
        break;
    case InputEvent::Type::PointerLeave:
        m_inside = false;
        m_pointer = {-1.0f, -1.0f};
        break;
    case InputEvent::Type::PointerDown:
        m_pointer = e.position;
        m_inside = true;
        m_down[index(e.button)] = true;
        m_pressed[index(e.button)] = true;
        break;
    case InputEvent::Type::PointerUp:
        m_pointer = e.position;
        m_down[index(e.button)] = false;
        m_released[index(e.button)] = true;
        break;
    case InputEvent::Type::Wheel:
        m_wheel.x += e.wheel.x;
        m_wheel.y += e.wheel.y;
        break;
    case InputEvent::Type::KeyDown:
        m_keys.push_back(e.key);
        break;
    case InputEvent::Type::KeyUp:
        break;
    case InputEvent::Type::Text:
        m_text += e.text;
        break;
    }
}

} // namespace zodar::ui
