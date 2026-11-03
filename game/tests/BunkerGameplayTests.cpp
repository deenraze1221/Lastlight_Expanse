#include "Bunker/BunkerGameplay.h"
#include "Player/ExplorationWalker.h"
#include "SceneFile/SceneFile.h"
#include "SceneFile/SaveGame.h"
#include "WorldRender/SceneComponents.h"
#include <doctest/doctest.h>

using namespace lastlight;
using namespace zodar;

TEST_CASE("D7: interactions respect range, occlusion and nearest target") {
    World world;
    const auto panel=world.create();
    world.add<Transform>(panel).position={0,0,-2};
    world.add<SceneId>(panel,SceneId{"panel"});
    world.add<Interactable>(panel);
    world.add<ElectricalPanel>(panel,ElectricalPanel{"room",false});
    CollisionWorld collision;
    CHECK(interactionTarget(world,collision,{0,1.6f,0},{0,0,-1})=="panel");
    CHECK(interactionTarget(world,collision,{0,1.6f,2},{0,0,-1}).empty());
    collision.addStatic({{-1,0,-1},{1,3,-0.9f}});
    CHECK(interactionTarget(world,collision,{0,1.6f,0},{0,0,-1}).empty());
    collision.clear();
    const auto door=world.create();
    world.add<Transform>(door).position={0,0,-1};
    world.add<SceneId>(door,SceneId{"door"});
    world.add<Interactable>(door);
    CHECK(interactionTarget(world,collision,{0,1.6f,0},{0,0,-1})=="door");
    useObject(world,"panel");
    CHECK(world.get<ElectricalPanel>(panel).repaired);
}

TEST_CASE("D7: sliding doors sweep their full travel and reopen on obstruction") {
    World world;
    const auto e=world.create();
    world.add<SceneId>(e,SceneId{"door"});
    world.add<Transform>(e).position={2,0,0};
    world.add<Collider>(e,Collider{{0,0,0},{1,2,0.1f},true});
    world.add<Interactable>(e);
    world.add<Door>(e,Door{{0,0,0},{2,0,0},1,1,true,false});
    useObject(world,"door");
    advanceDoors(world,{{0.5f,0,-0.1f},{1.5f,1.8f,0.3f}},2);
    CHECK(world.get<Door>(e).fraction==1);
    CHECK(world.get<Door>(e).open);
    CHECK(world.get<Door>(e).obstructed);
    useObject(world,"door");
    advanceDoors(world,{{10,0,0},{11,2,1}},0.5);
    CHECK(world.get<Door>(e).fraction==0.5f);
    CollisionWorld collision;
    addGameplayColliders(world,collision);
    CHECK(collision.overlaps({{1.1f,0,0},{1.2f,1,0.05f}}));
    advanceDoors(world,{{10,0,0},{11,2,1}},0.5);
    CHECK(world.get<Door>(e).fraction==0);
    validateBunker(world);
    const auto doc=captureScene(world,{});
    World loaded;
    instantiateScene(parseScene(serializeScene(doc)),loaded,{});
    CHECK(gameplayFingerprint(loaded)==gameplayFingerprint(world));
    validateBunker(loaded);
}

TEST_CASE("D7: room power requires generator and repaired panel, emergency is inverse") {
    World world;
    const auto generator=world.create(), panel=world.create(), main=world.create(), emergency=world.create();
    world.add<PowerSwitch>(generator);
    world.add<ElectricalPanel>(panel,ElectricalPanel{"room",false});
    world.add<PointLightComponent>(main);
    world.add<EmergencyLight>(main,EmergencyLight{"room",12,false});
    world.add<PointLightComponent>(emergency);
    world.add<EmergencyLight>(emergency,EmergencyLight{"room",3,true});
    for (bool running:{false,true}) for (bool repaired:{false,true}) {
        world.get<PowerSwitch>(generator).enabled=running;
        world.get<ElectricalPanel>(panel).repaired=repaired;
        updatePower(world);
        CHECK(world.get<PointLightComponent>(main).intensity==((running&&repaired)?12:0));
        CHECK(world.get<PointLightComponent>(emergency).intensity==((running&&repaired)?0:3));
        CHECK_FALSE(roomPowered(world,"other"));
    }
}

TEST_CASE("D7: crouch blocks standing beneath ceiling and survives save roundtrip") {
    CollisionWorld world;
    world.addStatic({{-10,-1,-10},{10,0,10}});
    ExplorationWalker player;
    player.spawn({0,0,0});
    WalkInput controls;
    controls.crouch=true;
    player.step(world,controls,1.0f/60);
    CHECK(player.crouched());
    CHECK(player.box().max.y==doctest::Approx(1.101f).epsilon(0.01));
    world.addStatic({{-2,1.2f,-2},{2,2,2}});
    controls.crouch=false;
    player.step(world,controls,1.0f/60);
    CHECK(player.crouched());
    CHECK(player.stage(player.capture()).capture()==player.capture());
    world.clear();
    player.step(world,controls,1.0f/60);
    CHECK_FALSE(player.crouched());
}

TEST_CASE("D7: five-minute autosave uses the fixed simulation clock") {
    SaveGameDesc settings;
    settings.gameId="LastlightExpanse";
    settings.autosaveInterval=300;
    CHECK_FALSE(autosaveDue(settings,5998,5999,0.05));
    CHECK(autosaveDue(settings,5999,6000,0.05));
    CHECK_FALSE(autosaveDue(settings,6000,6001,0.05));
    CHECK(autosaveDue(settings,11999,12000,0.05));
}

TEST_CASE("D7: gameplay fingerprints survive hierarchy capture reordering") {
    World world;
    const auto root=world.create(), other=world.create(), child=world.create();
    world.add<SceneId>(root,SceneId{"room"});
    world.add<SceneId>(other,SceneId{"generator"});
    world.add<PowerSwitch>(other,PowerSwitch{true});
    world.add<SceneId>(child,SceneId{"panel"});
    world.add<ElectricalPanel>(child,ElectricalPanel{"room",true});
    world.setParent(child,root);
    const auto expected=gameplayFingerprint(world);
    const auto document=captureScene(world,{});
    World restored;
    const auto instance=instantiateScene(document,restored,{});
    (void)instance;
    CHECK(gameplayFingerprint(restored)==expected);
}
