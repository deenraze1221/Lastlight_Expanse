#pragma once

namespace raven {

// Owns process-wide platform initialization (SDL video subsystem).
// Exactly one instance must outlive every Window.
class Platform {
public:
    Platform();
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    // Name of the active video backend, e.g. "windows", "x11", "wayland", "offscreen".
    const char* videoDriver() const;
};

} // namespace raven
