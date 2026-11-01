#pragma once

#include "Physics/CollisionWorld.h"

#include <glm/glm.hpp>

namespace lastlight {

// TEMPORARY first-person walker for exploring the bunker (`--explore`), until the D7 player
// controller (driven by D3 input actions) replaces it. Deliberately minimal: an upright box
// moved through CollisionWorld with gravity, jumping, sprinting, stair step-up and camera
// smoothing. No input handling of its own (the caller fills WalkInput), no gameplay state.
struct WalkInput {
    float forward = 0.0f; // -1..1
    float right = 0.0f;   // -1..1
    bool sprint = false;
    bool jump = false;    // held; jumps when grounded
    float yaw = 0.0f;     // radians, camera yaw (forward = (-sin, 0, -cos))
};

struct WalkerSettings {
    float halfWidth = 0.3f;    // box 0.6 x 1.8 x 0.6 m
    float height = 1.8f;
    float eyeHeight = 1.65f;
    float walkSpeed = 4.0f;    // m/s
    float sprintSpeed = 7.0f;
    float acceleration = 14.0f; // 1/s, exponential approach to the target velocity
    float jumpSpeed = 4.8f;    // ~0.75 m jump with the gravity below
    float gravity = 15.0f;
    float stepHeight = 0.35f;  // climbs stair steps and small ledges
};

class ExplorationWalker {
public:
    ExplorationWalker() = default;
    explicit ExplorationWalker(const WalkerSettings& settings) : m_settings(settings) {}

    // Feet position (centre of the box bottom).
    void spawn(const glm::vec3& feet);
    void step(const CollisionWorld& world, const WalkInput& input, float dt);

    const glm::vec3& feet() const { return m_feet; }
    glm::vec3 eye() const { return m_feet + glm::vec3(0.0f, m_settings.eyeHeight + m_eyeLag, 0.0f); }
    bool grounded() const { return m_grounded; }
    const glm::vec3& velocity() const { return m_velocity; }
    const WalkerSettings& settings() const { return m_settings; }
    Aabb box() const { return boxAt(m_feet); }

private:
    Aabb boxAt(const glm::vec3& feet) const;

    WalkerSettings m_settings;
    glm::vec3 m_spawn{0.0f};
    glm::vec3 m_feet{0.0f};
    glm::vec3 m_velocity{0.0f};
    float m_eyeLag = 0.0f; // smooths the camera over step-ups
    bool m_grounded = false;
};

} // namespace lastlight
