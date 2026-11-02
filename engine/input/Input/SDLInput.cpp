#include "Input/Input.h"
#include <SDL3/SDL.h>

namespace zodar {
bool validInputCode(std::string_view source, std::string& code) {
    if(source=="key") {
        const auto scancode=SDL_GetScancodeFromName(code.c_str());
        if(scancode==SDL_SCANCODE_UNKNOWN) return false;
        // SDL accepts aliases/case-insensitive names; events always use canonical names.
        code=SDL_GetScancodeName(scancode);
        return true;
    }
    if(source=="mouseButton") return code=="left" || code=="right" || code=="middle";
    if(source=="mouseDelta" || source=="wheel") return code=="x" || code=="y";
    if(source=="gamepadButton") return SDL_GetGamepadButtonFromString(code.c_str())!=SDL_GAMEPAD_BUTTON_INVALID;
    if(source=="gamepadAxis") return SDL_GetGamepadAxisFromString(code.c_str())!=SDL_GAMEPAD_AXIS_INVALID;
    return false;
}
void Input::processEvent(const SDL_Event& e) {
    switch(e.type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        if(!e.key.repeat) feed("key",SDL_GetScancodeName(e.key.scancode),e.type==SDL_EVENT_KEY_DOWN?1.0f:0.0f);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: {
        const char* name=e.button.button==SDL_BUTTON_LEFT?"left":e.button.button==SDL_BUTTON_RIGHT?"right":e.button.button==SDL_BUTTON_MIDDLE?"middle":"other";
        feed("mouseButton",name,e.type==SDL_EVENT_MOUSE_BUTTON_DOWN?1.0f:0.0f); break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        feed("mouseDelta","x",e.motion.xrel); feed("mouseDelta","y",e.motion.yrel); break;
    case SDL_EVENT_MOUSE_WHEEL: {
        const float sign=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1.0f:1.0f;
        feed("wheel","x",e.wheel.x*sign); feed("wheel","y",e.wheel.y*sign); break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if(const char* name=SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(e.gbutton.button)))
            feed("gamepadButton",name,e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN?1.0f:0.0f);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if(const char* name=SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(e.gaxis.axis)))
            feed("gamepadAxis",name,static_cast<float>(e.gaxis.value)/(e.gaxis.value<0?32768.0f:32767.0f));
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: case SDL_EVENT_GAMEPAD_REMOVED: resetControls(); break;
    default: break;
    }
}
} // namespace zodar
