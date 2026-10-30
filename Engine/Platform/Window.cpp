#include "Platform/Window.h"

#include "Core/Error.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace raven {

Window::Window(const WindowDesc& desc) {
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    m_window = SDL_CreateWindow(desc.title.c_str(), static_cast<int>(desc.width),
                                static_cast<int>(desc.height), flags);
    if (!m_window) {
        fail(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    const auto size = framebufferSize();
    RAVEN_LOG_INFO("Window created: '{}' {}x{} (drawable {}x{})", desc.title, desc.width, desc.height,
                   size.width, size.height);
}

Window::~Window() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
}

void Window::pollEvents(const EventCallback& callback) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            m_shouldClose = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            m_resized = true;
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            m_minimized = true;
            break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
            m_minimized = false;
            m_resized = true;
            break;
        default:
            break;
        }
        if (callback) {
            callback(event);
        }
    }
}

bool Window::consumeResized() {
    const bool resized = m_resized;
    m_resized = false;
    return resized;
}

FramebufferSize Window::framebufferSize() const {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    return {static_cast<uint32_t>(width > 0 ? width : 0), static_cast<uint32_t>(height > 0 ? height : 0)};
}

PFN_vkGetInstanceProcAddr Window::vulkanGetInstanceProcAddr() const {
    auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!proc) {
        fail(std::string("Vulkan loader unavailable: ") + SDL_GetError());
    }
    return proc;
}

std::vector<const char*> Window::requiredInstanceExtensions() const {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) {
        fail(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
    return {names, names + count};
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface)) {
        fail(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
    return surface;
}

void Window::destroySurface(VkInstance instance, VkSurfaceKHR surface) const {
    SDL_Vulkan_DestroySurface(instance, surface, nullptr);
}

} // namespace raven
