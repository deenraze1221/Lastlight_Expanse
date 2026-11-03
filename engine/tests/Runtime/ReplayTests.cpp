#include "Input/Recording.h"
#include "Core/Random.h"
#include "Runtime/CommandLine.h"
#include "SceneFile/DataError.h"
#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
using namespace zodar;
TEST_CASE("replay: delta roundtrip, strict compatibility and truncation") {
    InputRecording r; r.header={"test","map","level",42,1.0/60.0};
    r.ticks={{{{"jump",{1,0,true,true,false}}},"one"},{{{"jump",{1,0,true,false,false}}},"two"},{{{"jump",{}}},"three"}};
    const auto bytes=r.serialize();
    const auto copy=InputRecording::parse(bytes);
    CHECK(copy.serialize()==bytes); CHECK(copy.ticks.size()==3);
    CHECK(copy.ticks[1].actions.at("jump").held);
    CHECK_NOTHROW(copy.checkCompatible(r.header));
    auto wrong=r.header; wrong.levelHash="changed";
    CHECK_THROWS(copy.checkCompatible(wrong)); CHECK_NOTHROW(copy.checkCompatible(wrong,true));
    wrong=r.header; wrong.seed=9; CHECK_THROWS(copy.checkCompatible(wrong,true));
    wrong=r.header; wrong.fixedDt=0.1; CHECK_THROWS(copy.checkCompatible(wrong));
    CHECK_THROWS_AS(InputRecording::parse(bytes.substr(0,bytes.size()-1)),DataError);
    CHECK_THROWS_AS(InputRecording::parse(bytes.substr(0,bytes.rfind('{'))),DataError);
    CHECK_THROWS_AS(InputRecording::parse(bytes+"{}\n"),DataError);
    auto broken=bytes; broken.replace(broken.find("one"),3,"bad"); CHECK_THROWS(InputRecording::parse(broken));
    CHECK_THROWS(InputRecording::parse("garbage\n"));
    CHECK_THROWS(InputRecording::parse("{\"schema\":1,\"schema\":2}\n"));
    InputRecording sparse=r; sparse.ticks[1].actions["extra"]={};
    CHECK_THROWS(InputRecording::parse(sparse.serialize()));
}
TEST_CASE("replay: named RNG streams and restored continuation") {
    Random a(123),b(123),different(124);
    const auto first=a.next("world");
    CHECK(first==2365262688111210330ull);
    b.next("other"); CHECK(b.next("world")==first); CHECK(different.next("world")!=first);
    auto state=a.states(); Random restored; restored.restore(a.seed(),state);
    for(int i=0;i<10;++i) CHECK(a.next("world")==restored.next("world"));
    for(int i=0;i<10;++i) { const auto v=a.unit("unit"); CHECK(v>=0); CHECK(v<1); }
}
TEST_CASE("replay: recorded actions reproduce simulation at fixed ticks") {
    const auto map=InputMap::parse(R"({"schema":"zodar.input_map","version":1,"contexts":[{"name":"game","actions":{"move":{"type":"axis1","bindings":[]},"jump":{"type":"button","bindings":[]}}}]})");
    Input live; live.setMap(map);
    InputRecording r; r.header={"test",map.hash(),"level",17,0.01};
    Random rng(17); double position=0;
    for(int tick=0;tick<12;++tick) {
        live.inject("move",{tick<6?1.0f:-1.0f});
        live.inject("jump",{tick==3?1.0f:0.0f,0,tick==3}); live.beginStep();
        position+=live.action("move").x*0.01;
        if(live.action("jump").pressed) position+=rng.unit("jump");
        r.ticks.push_back({live.snapshot(),std::to_string(position)});
    }
    const auto copy=InputRecording::parse(r.serialize());
    for(int run=0;run<2;++run) {
        Input replay; replay.setMap(map); Random rr(17); double actual=0;
        for(const auto& t:copy.ticks) {
            for(const auto& [name,s]:t.actions) replay.inject(name,s);
            replay.beginStep(); actual+=replay.action("move").x*0.01;
            if(replay.action("jump").pressed) actual+=rr.unit("jump");
            CHECK(std::to_string(actual)==t.state); CHECK(replay.snapshot()==t.actions);
        }
    }
}

TEST_CASE("replay: standard CLI exposes input map and recording options") {
    ApplicationDesc desc;
    const std::vector<std::string> args{"--input-map","controls.json","--replay-input","walk.jsonl","--replay-lenient","--seed","17","--fixed-dt","0.02"};
    CHECK(parseStandardOptions(args,desc).empty());
    CHECK(desc.inputMap==std::filesystem::path("controls.json"));
    CHECK(desc.replayInput==std::filesystem::path("walk.jsonl"));
    CHECK(desc.replayLenient); CHECK(desc.deterministic); CHECK(desc.seed==17);
    CHECK(desc.timestep.fixedDt==0.02);
    ApplicationDesc record;
    CHECK(parseStandardOptions(std::vector<std::string>{"--record-input","walk.jsonl"},record).empty());
    CHECK(record.recordInput==std::filesystem::path("walk.jsonl"));
}
