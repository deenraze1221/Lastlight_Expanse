#pragma once

#include "Physics/CollisionWorld.h"
#include "World/World.h"
#include "Gameplay/EventBus.h"
#include <string>

namespace lastlight {

// Authored model-local boxes. Architecture collision still comes from the voxel world.
struct Collider { glm::vec3 min{0}, max{1}; bool enabled = true; };
struct Interactable { glm::vec3 min{-0.4f,0,-0.1f}, max{0.4f,2,0.4f}; float range = 2.5f; };
struct Door {
    glm::vec3 closed{0}, slide{2.1f,0,0};
    float fraction = 1, seconds = 1;
    bool open = true, obstructed = false;
};
struct PowerSwitch { bool enabled = false; };
struct ElectricalPanel { std::string room; bool repaired = false; };
// Attached to a light source. The parent model shares its on/off state for emissive rendering.
struct EmergencyLight { std::string room; float intensity = 12; bool emergency = false; };
struct PlayerSpawn { glm::vec3 feet{6,0,4}; float yaw = 0; };
struct Used { std::string id; };

void registerBunkerComponents();
Aabb worldBox(const zodar::World& world, zodar::Entity entity, const glm::vec3& min, const glm::vec3& max);
void addGameplayColliders(const zodar::World& world, CollisionWorld& out, bool movingOnly = false);
std::string interactionTarget(const zodar::World& world, const CollisionWorld& collision,
                              const glm::vec3& eye, const glm::vec3& direction);
void useObject(zodar::World& world, const std::string& id);
void advanceDoors(zodar::World& world, const Aabb& player, double dt);
bool roomPowered(const zodar::World& world, const std::string& room);
bool lightEnabled(const zodar::World& world, const EmergencyLight& light);
void updatePower(zodar::World& world);
void validateBunker(const zodar::World& world);
std::string gameplayFingerprint(const zodar::World& world);

} // namespace lastlight
