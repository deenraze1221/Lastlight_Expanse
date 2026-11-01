#include "Player/ExplorationWalker.h"

#include <cmath>

namespace lastlight {

Aabb ExplorationWalker::boxAt(const glm::vec3& feet) const {
    const float w = m_settings.halfWidth;
    return {feet - glm::vec3(w, 0.0f, w), feet + glm::vec3(w, m_settings.height, w)};
}

void ExplorationWalker::spawn(const glm::vec3& feet) {
    m_spawn = feet;
    m_feet = feet;
    m_velocity = glm::vec3(0.0f);
    m_eyeLag = 0.0f;
    m_grounded = false;
}

void ExplorationWalker::step(const CollisionWorld& world, const WalkInput& input, float dt) {
    if (dt <= 0.0f) {
        return;
    }
    // Horizontal velocity: approach the input direction at walk or sprint speed.
    const glm::vec3 forward(-std::sin(input.yaw), 0.0f, -std::cos(input.yaw));
    const glm::vec3 right(std::cos(input.yaw), 0.0f, -std::sin(input.yaw));
    glm::vec3 wish = forward * input.forward + right * input.right;
    if (glm::length(wish) > 1.0f) wish = glm::normalize(wish);
    const glm::vec3 target = wish * (input.sprint ? m_settings.sprintSpeed : m_settings.walkSpeed);
    const float blend = 1.0f - std::exp(-m_settings.acceleration * dt);
    m_velocity.x += (target.x - m_velocity.x) * blend;
    m_velocity.z += (target.z - m_velocity.z) * blend;

    // Vertical: gravity, jump, landing and head bumps.
    if (input.jump && m_grounded) {
        m_velocity.y = m_settings.jumpSpeed;
        m_grounded = false;
    }
    m_velocity.y -= m_settings.gravity * dt;
    const bool wasGrounded = m_grounded;
    const CollisionWorld::SweepResult vertical = world.sweep(boxAt(m_feet), {0.0f, m_velocity.y * dt, 0.0f});
    m_feet.y += vertical.moved.y;
    m_grounded = vertical.hitY && m_velocity.y < 0.0f;
    if (vertical.hitY) {
        m_velocity.y = 0.0f;
    }

    // Horizontal with slide; step up onto stairs and low ledges when blocked on the ground.
    const glm::vec3 move(m_velocity.x * dt, 0.0f, m_velocity.z * dt);
    CollisionWorld::SweepResult flat = world.sweep(boxAt(m_feet), move);
    glm::vec3 next = m_feet + flat.moved;
    if ((flat.hitX || flat.hitZ) && (m_grounded || wasGrounded)) {
        const CollisionWorld::SweepResult up = world.sweep(boxAt(m_feet), {0.0f, m_settings.stepHeight, 0.0f});
        const glm::vec3 raised = m_feet + up.moved;
        const CollisionWorld::SweepResult over = world.sweep(boxAt(raised), move);
        const float flatDist = glm::length(glm::vec2(flat.moved.x, flat.moved.z));
        const float overDist = glm::length(glm::vec2(over.moved.x, over.moved.z));
        if (overDist > flatDist + 1e-4f) {
            const glm::vec3 across = raised + over.moved;
            const CollisionWorld::SweepResult down = world.sweep(boxAt(across), {0.0f, -up.moved.y, 0.0f});
            const glm::vec3 landed = across + down.moved;
            m_eyeLag -= landed.y - m_feet.y; // the camera follows the step smoothly
            next = landed;
            m_grounded = true;
        }
    }
    m_feet = next;

    // Stay on the ground when walking down stairs instead of floating off each step.
    if (wasGrounded && !m_grounded && m_velocity.y <= 0.0f) {
        const CollisionWorld::SweepResult snap = world.sweep(boxAt(m_feet), {0.0f, -m_settings.stepHeight, 0.0f});
        if (snap.hitY) {
            m_feet.y += snap.moved.y;
            m_eyeLag -= snap.moved.y;
            m_grounded = true;
            m_velocity.y = 0.0f;
        }
    }
    m_eyeLag *= std::exp(-12.0f * dt);
    if (std::abs(m_eyeLag) < 1e-4f) m_eyeLag = 0.0f;

    // Fell out of the level (a gap in the colliders): back to the spawn point.
    if (m_feet.y < m_spawn.y - 20.0f) {
        spawn(m_spawn);
    }
}

} // namespace lastlight
