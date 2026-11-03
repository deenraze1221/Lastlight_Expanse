#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

union SDL_Event;

namespace zodar {

struct ActionState {
    float x = 0, y = 0;
    bool held = false, pressed = false, released = false;
    bool operator==(const ActionState&) const = default;
};
using ActionSnapshot = std::map<std::string, ActionState>;

struct InputBinding {
    std::string source, code;
    float x = 1, y = 0, deadZone = 0;
    bool invert = false;
};
struct InputAction {
    std::string type;
    std::vector<InputBinding> bindings;
};
struct InputContext {
    std::string name;
    int priority = 0;
    std::map<std::string, InputAction> actions;
};
struct InputMap {
    std::vector<InputContext> contexts;
    static InputMap parse(std::string_view text, std::string_view file = "<input>");
    static InputMap load(const std::filesystem::path& file);
    std::string serialize() const;
    std::string hash() const;
};

// Main-thread input. SDL translation is implemented separately; gameplay only reads actions.
class Input {
public:
    std::string capture() const;
    Input stage(std::string_view bytes) const;
    void commit(Input&& staged) noexcept;
    void setMap(InputMap map);
    const InputMap& map() const { return m_map; }
    void setContext(std::string_view name, bool enabled);
    void processEvent(const SDL_Event& event);
    void setRelativeMouse(bool enabled); // applied by Application to its own window
    bool relativeMouse() const { return m_relativeMouse; }
    // Platform-independent control feed (delta sources accumulate until the next tick).
    void feed(std::string_view source, std::string_view code, float value);
    void resetControls();
    // Overrides one action at the next tick. Button edges are derived unless explicitly supplied.
    void inject(std::string_view action, ActionState state);
    void beginStep();
    const ActionState& action(std::string_view name) const;
    const ActionSnapshot& snapshot() const { return m_state; }
private:
    void evaluate();
    InputMap m_map;
    std::map<std::string, bool> m_enabled;
    std::map<std::string, float> m_controls;
    ActionSnapshot m_pending, m_state, m_injected;
    bool m_relativeMouse = false;
};

// Stable byte hash, independent of std::hash/library/platform. Also used for content identity.
std::string inputHash(std::string_view bytes);
} // namespace zodar
