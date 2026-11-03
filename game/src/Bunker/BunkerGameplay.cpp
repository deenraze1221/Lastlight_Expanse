#include "Bunker/BunkerGameplay.h"
#include "SceneFile/SceneFile.h"
#include "WorldRender/SceneComponents.h"
#include "Core/Error.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace lastlight {
using namespace zodar;

void registerBunkerComponents() {
    static const bool registered = [] {
#define FIELD(T, member, kind) {#member, FieldType::kind, ZODAR_FIELD_ACCESS(T, member), #member}
        static const FieldInfo collider[] = {FIELD(Collider,min,Vec3), FIELD(Collider,max,Vec3), FIELD(Collider,enabled,Bool)};
        static const FieldInfo interactable[] = {FIELD(Interactable,min,Vec3), FIELD(Interactable,max,Vec3),
            {"range",FieldType::Float,ZODAR_FIELD_ACCESS(Interactable,range),"Use distance",0.1f,5.0f}};
        static const FieldInfo door[] = {FIELD(Door,closed,Vec3), FIELD(Door,slide,Vec3),
            {"fraction",FieldType::Float,ZODAR_FIELD_ACCESS(Door,fraction),"Slide progress",0,1},
            {"seconds",FieldType::Float,ZODAR_FIELD_ACCESS(Door,seconds),"Travel time",0.1f,10},
            FIELD(Door,open,Bool), FIELD(Door,obstructed,Bool)};
        static const FieldInfo power[] = {FIELD(PowerSwitch,enabled,Bool)};
        static const FieldInfo panel[] = {FIELD(ElectricalPanel,room,String), FIELD(ElectricalPanel,repaired,Bool)};
        static const FieldInfo light[] = {FIELD(EmergencyLight,room,String),
            {"intensity",FieldType::Float,ZODAR_FIELD_ACCESS(EmergencyLight,intensity),"Lit intensity",0,1000},
            FIELD(EmergencyLight,emergency,Bool)};
        static const FieldInfo spawn[] = {FIELD(PlayerSpawn,feet,Vec3), FIELD(PlayerSpawn,yaw,Float)};
#undef FIELD
        registerSceneComponent<Collider>("collider",collider);
        registerSceneComponent<Interactable>("interactable",interactable);
        registerSceneComponent<Door>("door",door);
        registerSceneComponent<PowerSwitch>("powerSwitch",power);
        registerSceneComponent<ElectricalPanel>("electricalPanel",panel);
        registerSceneComponent<EmergencyLight>("emergencyLight",light);
        registerSceneComponent<PlayerSpawn>("playerSpawn",spawn);
        return true;
    }();
    (void)registered;
}

Aabb worldBox(const World& world, Entity entity, const glm::vec3& min, const glm::vec3& max) {
    const auto matrix = world.worldMatrix(entity);
    Aabb out{glm::vec3(std::numeric_limits<float>::max()),glm::vec3(std::numeric_limits<float>::lowest())};
    for (int i=0;i<8;++i) {
        const glm::vec3 p(matrix*glm::vec4((i&1)?max.x:min.x,(i&2)?max.y:min.y,(i&4)?max.z:min.z,1));
        out.min=glm::min(out.min,p); out.max=glm::max(out.max,p);
    }
    return out;
}

void addGameplayColliders(const World& world, CollisionWorld& out, bool movingOnly) {
    for (const auto e:world.entities()) if (const auto* c=world.tryGet<Collider>(e); c && c->enabled)
        if (!movingOnly || world.has<Door>(e)) out.addStatic(worldBox(world,e,c->min,c->max));
}

std::string interactionTarget(const World& world, const CollisionWorld& collision,
                              const glm::vec3& eye, const glm::vec3& direction) {
    float nearest=std::numeric_limits<float>::max();
    std::string target;
    if (glm::length(direction)<=0) return target;
    for (const auto e:world.entities()) {
        const auto* use=world.tryGet<Interactable>(e);
        const auto* id=world.tryGet<SceneId>(e);
        if (!use || !id) continue;
        CollisionWorld bounds;
        const auto box=worldBox(world,e,use->min,use->max);
        bounds.addStatic(box);
        const auto hit=bounds.raycast(eye,direction,use->range);
        // An interaction volume can extend into walkable space. Starting within it is
        // a zero-distance target, even though collision rays ignore enclosing solids.
        const bool inside=glm::all(glm::greaterThanEqual(eye,box.min)) && glm::all(glm::lessThanEqual(eye,box.max));
        if (!inside && !hit) continue;
        const float distance=inside?0.0f:hit->distance;
        if (distance>nearest || (distance==nearest && id->value>=target)) continue;
        // The target's collider may coincide with its interaction surface. An earlier
        // wall, prop or closed door blocks it; geometry behind the surface does not.
        const auto obstruction=collision.raycast(eye,direction,distance);
        if (obstruction && obstruction->distance+0.002f<distance) continue;
        nearest=distance; target=id->value;
    }
    return target;
}

void useObject(World& world, const std::string& id) {
    for (const auto e:world.entities()) {
        const auto* sceneId=world.tryGet<SceneId>(e);
        if (!sceneId || sceneId->value!=id || !world.has<Interactable>(e)) continue;
        if (auto* d=world.tryGet<Door>(e)) { d->open=!d->open; d->obstructed=false; }
        if (auto* p=world.tryGet<PowerSwitch>(e)) p->enabled=!p->enabled;
        if (auto* p=world.tryGet<ElectricalPanel>(e)) p->repaired=true;
        return;
    }
}

void advanceDoors(World& world, const Aabb& player, double dt) {
    for (const auto e:world.entities()) {
        auto* door=world.tryGet<Door>(e);
        if (!door) continue;
        auto& transform=world.get<Transform>(e);
        const auto& collider=world.get<Collider>(e);
        const float next=std::clamp(door->fraction+static_cast<float>(dt)/door->seconds*(door->open?1.0f:-1.0f),0.0f,1.0f);
        const auto previous=transform.position;
        const auto before=worldBox(world,e,collider.min,collider.max);
        transform.position=door->closed+door->slide*next;
        const auto after=worldBox(world,e,collider.min,collider.max);
        // Test the entire swept volume, including a full travel in one large fixed step.
        const Aabb swept{glm::min(before.min,after.min),glm::max(before.max,after.max)};
        door->obstructed=next!=door->fraction && swept.overlaps(player);
        if (door->obstructed) {
            transform.position=previous;
            door->open=true; // reopen rather than crush or trap the player
        } else door->fraction=next;
    }
}

bool roomPowered(const World& world, const std::string& room) {
    bool generator=false, panel=false;
    world.each<PowerSwitch>([&](Entity,const PowerSwitch& p){ generator=generator||p.enabled; });
    world.each<ElectricalPanel>([&](Entity,const ElectricalPanel& p){ if(p.room==room) panel=panel||p.repaired; });
    return generator && panel;
}
bool lightEnabled(const World& world, const EmergencyLight& light) {
    return roomPowered(world,light.room)!=light.emergency;
}
void updatePower(World& world) {
    for (const auto e:world.entities()) if (const auto* light=world.tryGet<EmergencyLight>(e))
        world.get<PointLightComponent>(e).intensity=lightEnabled(world,*light)?light->intensity:0;
}

void validateBunker(const World& world) {
    for (const auto e:world.entities()) {
        const auto checkBox=[](const glm::vec3& min,const glm::vec3& max) {
            if (glm::any(glm::greaterThanEqual(min,max))) fail("bunker box must have positive dimensions");
        };
        if (const auto* c=world.tryGet<Collider>(e)) checkBox(c->min,c->max);
        if (const auto* c=world.tryGet<Interactable>(e)) checkBox(c->min,c->max);
        if (const auto* d=world.tryGet<Door>(e)) {
            if (!world.has<Collider>(e) || !world.has<Transform>(e) || glm::length(d->slide)<0.01f)
                fail("door needs a collider, transform and slide displacement");
            if (glm::length(world.get<Transform>(e).position-(d->closed+d->slide*d->fraction))>0.001f)
                fail("door transform disagrees with progress");
        }
        if (world.has<EmergencyLight>(e) && !world.has<PointLightComponent>(e)) fail("room light needs pointLight");
    }
}
std::string gameplayFingerprint(const World& world) {
    nlohmann::json state=nlohmann::json::array();
    auto entities=world.entities();
    std::ranges::sort(entities,[&](Entity a,Entity b) {
        const auto* left=world.tryGet<SceneId>(a); const auto* right=world.tryGet<SceneId>(b);
        return (left?left->value:std::string{})<(right?right->value:std::string{});
    });
    for (const auto e:entities) {
        const auto* id=world.tryGet<SceneId>(e);
        if (!id) continue;
        if (const auto* d=world.tryGet<Door>(e)) state.push_back({id->value,d->fraction,d->open,d->obstructed});
        if (const auto* p=world.tryGet<PowerSwitch>(e)) state.push_back({id->value,p->enabled});
        if (const auto* p=world.tryGet<ElectricalPanel>(e)) state.push_back({id->value,p->room,p->repaired});
    }
    return state.dump();
}
} // namespace lastlight
