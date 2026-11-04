#include "Input/Input.h"
#include "SceneFile/DataError.h"
#include "Core/Error.h"
#include <limits>
#include <doctest/doctest.h>
#include <SDL3/SDL.h>
using namespace zodar;
TEST_CASE("input: finite controls and bindings cannot publish overflowing actions") {
    Input input;
    input.setMap(InputMap::parse(R"({"schema":"zodar.input_map","version":1,"contexts":[
        {"name":"game","actions":{"look":{"type":"axis1","bindings":[
        {"source":"mouseDelta","code":"x","scale":[3e38,0]}]}}}]})"));
    CHECK_THROWS_AS(input.feed("mouseDelta", "x", 2), Error);
    input.resetControls();
    input.beginStep();
    CHECK(input.action("look").x == 0);
    input.setMap(InputMap{});
    input.feed("mouseDelta", "x", std::numeric_limits<float>::max());
    CHECK_THROWS_AS(input.feed("mouseDelta", "x", std::numeric_limits<float>::max()), Error);
    input.setMap(InputMap::parse(R"({"schema":"zodar.input_map","version":1,"contexts":[
        {"name":"game","actions":{"disabled":{"type":"button","bindings":[
        {"source":"gamepadAxis","code":"leftx","deadZone":0.5,"scale":[0,0]}]}}}]})"));
    CHECK_THROWS_AS(input.feed("gamepadAxis", "leftx", std::numeric_limits<float>::max()), Error);
    input.resetControls();
    input.beginStep();
    CHECK_FALSE(input.action("disabled").held);
    CHECK_FALSE(input.action("disabled").pressed);
}
namespace {
InputMap map() { return InputMap::parse(R"({"schema":"zodar.input_map","version":1,"contexts":[
{"name":"game","priority":0,"actions":{
"jump":{"type":"button","bindings":[{"source":"key","code":"Space"}]},
"look":{"type":"axis2","bindings":[{"source":"mouseDelta","code":"x"},{"source":"mouseDelta","code":"y","scale":[0,1]}]},
"move":{"type":"axis1","bindings":[{"source":"gamepadAxis","code":"leftx","deadZone":0.2}]}}},
{"name":"menu","priority":2,"actions":{"accept":{"type":"button","bindings":[{"source":"key","code":"Space"}]}}}]})"); }
}
TEST_CASE("input: edges, taps, deltas, focus reset and contexts") {
    Input i; i.setMap(map()); i.setContext("menu",false);
    i.feed("key","Space",1); i.feed("key","Space",0);
    i.feed("mouseDelta","x",2); i.feed("mouseDelta","x",3); i.feed("mouseDelta","y",-4);
    i.beginStep();
    CHECK(i.action("jump").pressed); CHECK(i.action("jump").released); CHECK_FALSE(i.action("jump").held);
    CHECK(i.action("look").x==5); CHECK(i.action("look").y==-4);
    i.beginStep(); CHECK_FALSE(i.action("jump").pressed); CHECK(i.action("look").x==0);
    i.feed("key","Space",1); i.beginStep(); CHECK(i.action("jump").held);
    i.beginStep(); CHECK(i.action("jump").held); CHECK_FALSE(i.action("jump").pressed);
    i.setContext("menu",true); i.beginStep(); CHECK(i.action("jump").released); CHECK(i.action("accept").pressed);
    i.resetControls(); i.beginStep(); CHECK(i.action("accept").released);
    i.feed("gamepadAxis","leftx",0.1f); i.beginStep(); CHECK(i.action("move").x==0);
    i.feed("gamepadAxis","leftx",0.6f); i.beginStep(); CHECK(i.action("move").x==doctest::Approx(0.5));
}
TEST_CASE("input: map roundtrip, validation and fixed-step injection") {
    auto m=map(); CHECK(InputMap::parse(m.serialize()).serialize()==m.serialize()); CHECK(m.hash()==map().hash());
    CHECK_THROWS_AS(InputMap::parse("{\n bad"),DataError);
    CHECK_THROWS_AS(InputMap::parse(R"({"schema":"zodar.input_map","version":1,"contexts":[],"typo":1})"),DataError);
    Input i; i.setMap(m);
    i.inject("jump",{1,0,true}); i.beginStep(); CHECK(i.action("jump").pressed);
    i.inject("jump",{1,0,true}); i.beginStep(); CHECK_FALSE(i.action("jump").pressed); CHECK(i.action("jump").held);
    i.inject("jump",{}); i.beginStep(); CHECK(i.action("jump").released);
    i.inject("jump",{1,0,true}); i.beginStep(); CHECK(i.action("jump").pressed);
    i.beginStep(); CHECK(i.action("jump").released); // one-tick injection expires
    CHECK_THROWS(i.inject("unknown",{})); CHECK_THROWS(i.setContext("unknown",true));
}

TEST_CASE("input: SDL adapter preserves edges and consumes motion once") {
    Input i; i.setMap(map()); i.setContext("menu",false);
    SDL_Event e{}; e.type=SDL_EVENT_KEY_DOWN; e.key.scancode=SDL_SCANCODE_SPACE;
    i.processEvent(e); i.beginStep(); CHECK(i.action("jump").pressed);
    e.key.repeat=true; i.processEvent(e); i.beginStep(); CHECK_FALSE(i.action("jump").pressed);
    e.type=SDL_EVENT_KEY_UP; e.key.repeat=false; i.processEvent(e);
    e={}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.xrel=3; e.motion.yrel=-2; i.processEvent(e);
    i.beginStep(); CHECK(i.action("jump").released); CHECK(i.action("look").x==3); CHECK(i.action("look").y==-2);
    i.beginStep(); CHECK(i.action("look").x==0);
    e={}; e.type=SDL_EVENT_GAMEPAD_AXIS_MOTION; e.gaxis.axis=SDL_GAMEPAD_AXIS_LEFTX; e.gaxis.value=32767;
    i.processEvent(e); i.beginStep(); CHECK(i.action("move").x==1);
    e={}; e.type=SDL_EVENT_WINDOW_FOCUS_LOST; i.processEvent(e); i.beginStep(); CHECK(i.action("move").x==0);
    auto bytes=map().serialize(); bytes.replace(bytes.find("Space"),5,"BogusKey");
    CHECK_THROWS_AS(InputMap::parse(bytes),DataError);
}

TEST_CASE("input: inverted axis bindings and wheel deltas roundtrip") {
    auto m=map();
    auto& b=m.contexts[0].actions.at("move").bindings[0]; b.invert=true;
    m.contexts[0].actions["wheel"]={"axis1",{{"wheel","y",1,0,0,false}}};
    Input i; i.setMap(InputMap::parse(m.serialize()));
    i.feed("gamepadAxis","leftx",-0.6f); i.feed("wheel","y",2); i.feed("wheel","y",-1);
    i.beginStep(); CHECK(i.action("move").x==doctest::Approx(0.5f)); CHECK(i.action("wheel").x==1);
    i.beginStep(); CHECK(i.action("wheel").x==0);
}

TEST_CASE("input: duplicate fields and nonintegral metadata are rejected") {
    CHECK_THROWS_AS(InputMap::parse(R"({"schema":"zodar.input_map","schema":"zodar.input_map","version":1,"contexts":[]})"),DataError);
    CHECK_THROWS_AS(InputMap::parse(R"({"schema":"zodar.input_map","version":1.0,"contexts":[]})"),DataError);
    auto bytes=map().serialize();
    const auto priority=bytes.find("\"priority\":0");
    bytes.replace(priority,12,"\"priority\":0.5");
    CHECK_THROWS_AS(InputMap::parse(bytes),DataError);
}

TEST_CASE("input: SDL key aliases are canonical and button bindings combine with OR") {
    auto bytes=map().serialize(); bytes.replace(bytes.find("Space"),5,"sPaCe");
    CHECK(InputMap::parse(bytes).serialize()==map().serialize());
    auto m=InputMap::parse(bytes);
    m.contexts[0].actions.at("jump").bindings.push_back({"key","F",-1,0,0,false});
    Input i; i.setMap(m); i.setContext("menu",false);
    i.feed("key","Space",1); i.feed("key","F",1); i.beginStep();
    CHECK(i.action("jump").held); CHECK(i.action("jump").pressed);
    i.feed("key","Space",0); i.beginStep(); CHECK(i.action("jump").held); CHECK_FALSE(i.action("jump").released);
    i.feed("key","F",0); i.beginStep(); CHECK(i.action("jump").released);
}
