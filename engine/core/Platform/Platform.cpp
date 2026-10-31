#include "Platform/Platform.h"

#include "Core/Error.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>

namespace zodar {

Platform::Platform() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fail(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    const int version = SDL_GetVersion();
    ZODAR_LOG_INFO("SDL {}.{}.{} initialized (video driver: {})", SDL_VERSIONNUM_MAJOR(version),
                   SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), videoDriver());
}

Platform::~Platform() {
    SDL_Quit();
}

const char* Platform::videoDriver() const {
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver ? driver : "none";
}

} // namespace zodar

namespace zodar {

std::filesystem::path Platform::userDataPath(const std::string& organization, const std::string& application) {
    char* path = SDL_GetPrefPath(organization.c_str(), application.c_str());
    if (!path) {
        return std::filesystem::temp_directory_path() / application;
    }
    std::filesystem::path result(path);
    SDL_free(path);
    return result;
}

} // namespace zodar
