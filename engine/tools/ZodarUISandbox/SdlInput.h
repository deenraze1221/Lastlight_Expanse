#pragma once

#include "UI/Input.h"

#include <optional>

union SDL_Event;
struct SDL_Renderer;

namespace zodar::sandbox {

// Translates an SDL event (already converted to render coordinates) into the UI's own
// input vocabulary. Lives in the sandbox so Zodar::UI stays SDL-free.
std::optional<ui::InputEvent> translateEvent(const SDL_Event& event);

} // namespace zodar::sandbox
