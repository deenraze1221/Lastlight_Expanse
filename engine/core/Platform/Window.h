#pragma once

#include <volk.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

union SDL_Event;
struct SDL_Window;

namespace zodar {

struct WindowDesc {
    std::string title = "ZODAR Engine";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
};

struct FramebufferSize {
    uint32_t width = 0;
    uint32_t height = 0;
};

// Native window with Vulkan surface support. Requires a live Platform.
class Window {
public:
    using EventCallback = std::function<void(const SDL_Event&)>;

    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Pumps OS events. Quit and resize are handled here; every event is also
    // forwarded to the callback (input mapping arrives in a later milestone).
    void pollEvents(const EventCallback& callback = {});

    bool shouldClose() const { return m_shouldClose; }
    void requestClose() { m_shouldClose = true; }
    bool isMinimized() const { return m_minimized; }

    // True once after the drawable size changed.
    bool consumeResized();
    FramebufferSize framebufferSize() const;

    // Vulkan WSI glue.
    PFN_vkGetInstanceProcAddr vulkanGetInstanceProcAddr() const;
    std::vector<const char*> requiredInstanceExtensions() const;
    VkSurfaceKHR createSurface(VkInstance instance) const;
    void destroySurface(VkInstance instance, VkSurfaceKHR surface) const;

    SDL_Window* native() const { return m_window; }

private:
    SDL_Window* m_window = nullptr;
    bool m_shouldClose = false;
    bool m_minimized = false;
    bool m_resized = false;
};

} // namespace zodar
