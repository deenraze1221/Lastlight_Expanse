#include "SdlInput.h"

#include <SDL3/SDL.h>

namespace zodar::sandbox {
namespace {

ui::PointerButton button(Uint8 b) {
    if (b == SDL_BUTTON_RIGHT) return ui::PointerButton::Right;
    if (b == SDL_BUTTON_MIDDLE) return ui::PointerButton::Middle;
    return ui::PointerButton::Left;
}

ui::Key key(SDL_Keycode k) {
    switch (k) {
    case SDLK_TAB: return ui::Key::Tab;
    case SDLK_RETURN: case SDLK_KP_ENTER: return ui::Key::Enter;
    case SDLK_ESCAPE: return ui::Key::Escape;
    case SDLK_BACKSPACE: return ui::Key::Backspace;
    case SDLK_DELETE: return ui::Key::Delete;
    case SDLK_LEFT: return ui::Key::Left;
    case SDLK_RIGHT: return ui::Key::Right;
    case SDLK_UP: return ui::Key::Up;
    case SDLK_DOWN: return ui::Key::Down;
    case SDLK_HOME: return ui::Key::Home;
    case SDLK_END: return ui::Key::End;
    case SDLK_SPACE: return ui::Key::Space;
    default: return ui::Key::Unknown;
    }
}

} // namespace

std::optional<ui::InputEvent> translateEvent(const SDL_Event& e) {
    ui::InputEvent out;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        out.type = ui::InputEvent::Type::PointerMove;
        out.position = {e.motion.x, e.motion.y};
        return out;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        out.type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? ui::InputEvent::Type::PointerDown : ui::InputEvent::Type::PointerUp;
        out.position = {e.button.x, e.button.y};
        out.button = button(e.button.button);
        return out;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        out.type = ui::InputEvent::Type::PointerLeave;
        return out;
    case SDL_EVENT_MOUSE_WHEEL:
        out.type = ui::InputEvent::Type::Wheel;
        out.wheel = {e.wheel.x, e.wheel.y};
        return out;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        out.type = e.type == SDL_EVENT_KEY_DOWN ? ui::InputEvent::Type::KeyDown : ui::InputEvent::Type::KeyUp;
        out.key = key(e.key.key);
        out.shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
        out.ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
        return out;
    case SDL_EVENT_TEXT_INPUT:
        out.type = ui::InputEvent::Type::Text;
        out.text = e.text.text ? e.text.text : "";
        return out;
    default:
        return std::nullopt;
    }
}

} // namespace zodar::sandbox
