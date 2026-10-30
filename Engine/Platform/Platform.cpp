#include "Platform/Platform.h"

#include "Core/Error.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>

namespace raven {

Platform::Platform() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fail(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    const int version = SDL_GetVersion();
    RAVEN_LOG_INFO("SDL {}.{}.{} initialized (video driver: {})", SDL_VERSIONNUM_MAJOR(version),
                   SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), videoDriver());
}

Platform::~Platform() {
    SDL_Quit();
}

const char* Platform::videoDriver() const {
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver ? driver : "none";
}

} // namespace raven
