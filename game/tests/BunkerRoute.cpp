// Offline authoring of the action-only D7 acceptance recording. No simulation shortcuts:
// movement, interaction rays, event delay, collision and door ticks match BunkerApp.
#include "Bunker/BunkerGameplay.h"
#include "Bunker/BunkerVoxels.h"
#include "Bunker/LevelColliders.h"
#include "Player/ExplorationWalker.h"
#include "GameInput.h"
#include "Input/Recording.h"
#include "Renderer/Camera.h"
#include "Core/Error.h"
#include <cmath>
#include <iostream>

using namespace lastlight;
using namespace zodar;

void writeBunkerRoute(const std::filesystem::path& output) {
    const std::filesystem::path assets=LASTLIGHT_ASSETS_DIR;
    const auto map=InputMap::load(assets/"input.json");
    InputRecording recording;
    recording.header={"LastlightExpanse",map.hash(),inputHash(bunkerContentHash(assets)+"exploremanual:dormitory"),1337,0.05};
    const auto doc=loadSceneFile(assets/"levels/bunker.scene.json");
    auto cpu=doc;
    cpu.materials.clear();
    for (auto& e:cpu.entities) { e.modelRenderer.reset(); e.meshRenderer.reset(); }
    World world;
    const auto instance=instantiateScene(cpu,world,{});
    (void)instance;
    BunkerVoxels voxels(assets);
    const auto table=loadColliderTable(assets/"colliders.json");
    CollisionWorld collision;
    CollisionWorld staticCollision;
    staticCollision.setVoxelWorld(voxels.world.get());
    (void)buildLevelColliders(doc,table,nullptr,staticCollision);
    auto rebuild=[&] {
        collision=staticCollision;
        addGameplayColliders(world,collision,true);
    };
    rebuild();
    ExplorationWalker player;
    player.spawn({6,0,4});
    float yaw=0, pitch=0;
    std::string pending;
    auto tick=[&](float forward,float desiredYaw,float desiredPitch,bool use=false) {
        ActionSnapshot actions;
        for (const auto& context:map.contexts) for (const auto& [name,a]:context.actions) { (void)a; actions[name]={}; }
        actions["move"].y=forward;
        actions["look"]={-(desiredYaw-yaw)/0.0025f,-(desiredPitch-pitch)/0.0025f,false,false,false};
        actions["interact"]={use?1.0f:0.0f,0,use,use,false};
        yaw-=actions["look"].x*0.0025f;
        pitch=std::clamp(pitch-actions["look"].y*0.0025f,-1.5f,1.5f);
        WalkInput walk; walk.forward=forward; walk.yaw=yaw;
        player.step(collision,walk,0.05f);
        if (!pending.empty()) { useObject(world,pending); pending.clear(); }
        if (use) {
            gfx::Camera camera; camera.setYawPitch(yaw,pitch);
            pending=interactionTarget(world,collision,player.eye(),camera.forward());
            if (pending.empty()) {
                for (const auto e:world.entities()) if (world.has<Door>(e)) {
                    const auto& useBox=world.get<Interactable>(e);
                    const auto box=worldBox(world,e,useBox.min,useBox.max);
                    std::cerr<<world.get<SceneId>(e).value<<" bounds "<<box.min.x<<","<<box.min.z<<" - "<<box.max.x<<","<<box.max.z<<'\n';
                }
                const auto hit=collision.raycast(player.eye(),camera.forward(),2.5f);
                if (hit) std::cerr<<"occluder "<<hit->point.x<<","<<hit->point.y<<","<<hit->point.z<<" distance "<<hit->distance<<'\n';
                fail("route interaction missed at "+std::to_string(player.feet().x)+","+std::to_string(player.feet().z));
            }
            std::cout<<"route uses "<<pending<<'\n';
        }
        updatePower(world);
        advanceDoors(world,player.box(),0.05);
        rebuild();
        recording.ticks.push_back({std::move(actions),{}});
    };
    auto walkTo=[&](float x,float z) {
        for (int i=0;i<600;++i) {
            const glm::vec2 delta(x-player.feet().x,z-player.feet().z);
            if (glm::length(delta)<0.15f) {
                for(int stop=0;stop<12;++stop) tick(0,yaw,0);
                return;
            }
            tick(1,std::atan2(-delta.x,-delta.y),0);
        }
        fail("route blocked toward "+std::to_string(x)+","+std::to_string(z)+" at "+std::to_string(player.feet().x)+","+std::to_string(player.feet().z));
    };
    auto useAt=[&](glm::vec3 target) {
        const auto d=target-player.eye();
        tick(0,std::atan2(-d.x,-d.z),std::atan2(d.y,glm::length(glm::vec2(d.x,d.z))),true);
        for(int i=0;i<25;++i) tick(0,yaw,pitch);
    };
    tick(0,0,0);
    useAt({4,1.5f,2.7f}); // dormitory panel
    walkTo(6,0);
    walkTo(21,0);
    useAt({22,1.5f,1.4f}); // corridor panel
    walkTo(50,0);
    walkTo(50,4.5f);
    useAt({51,1.3f,7}); // generator
    walkTo(50,0);
    walkTo(6,0);
    walkTo(6,3.5f);
    useAt({8,1.5f,2.56f}); // use the parked leaf from inside; the wall occludes it outside
    useAt({6,1.5f,2.56f}); // reopen, then enter through it
    walkTo(6,0);
    walkTo(-4,0); // airlock
    if (!roomPowered(world,"dormitory") || !roomPowered(world,"corridor")) fail("route did not restore power");
    recording.save(output);
    std::cout<<"wrote "<<recording.ticks.size()<<" action ticks\n";
}
