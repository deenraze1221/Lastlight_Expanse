#include "SceneFile/SaveGame.h"
#include "Core/AtomicFile.h"
#include "Core/Random.h"
#include "Gameplay/Gameplay.h"
#include "Input/Input.h"
#include <doctest/doctest.h>
#include <fstream>

namespace {
struct SavedLink { uint32_t amount=1; std::string target; };
void registerLink() {
    static const bool registered=[] {
        static const zodar::FieldInfo fields[]={
            {"amount",zodar::FieldType::Uint,ZODAR_FIELD_ACCESS(SavedLink,amount),"Amount",1,100},
            {"target",zodar::FieldType::EntityReference,ZODAR_FIELD_ACCESS(SavedLink,target),"Stable scene ID"}};
        zodar::registerSceneComponent<SavedLink>("savedLink",fields); return true;
    }();
    (void)registered;
}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/"zodar-d6-save-tests";
    Temp() { std::filesystem::remove_all(path); std::filesystem::create_directories(path); }
    ~Temp() { std::filesystem::remove_all(path); }
};
std::string bytes(const std::filesystem::path& p) {
    std::ifstream in(p,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}};
}
}
TEST_CASE("D6 registered components: strict fields, stable references, prefab patch and round trip") {
    (void)zodar::parseScene(R"({"version":1,"entities":[{}]})");
    registerLink(); Temp temp;
    const auto prefab=temp.path/"link.prefab.json";
    zodar::writeFileAtomically(prefab,R"({"version":1,"entities":[{"id":"a","savedLink":{"amount":7,"target":"b"}},{"id":"b","savedLink":{"target":"a"}}]})");
    const auto doc=zodar::parseScene(R"({"version":1,"entities":[{"id":"p","prefab":"link.prefab.json","overrides":{"a":{"savedLink":{"amount":9}}}}]})","scene",temp.path);
    zodar::World world;
    const auto instance=zodar::instantiateScene(doc,world,{});
    CHECK(world.get<SavedLink>(instance.byId.at("p/a")).amount==9);
    CHECK(world.get<SavedLink>(instance.byId.at("p/a")).target=="p/b");
    CHECK(world.get<SavedLink>(instance.byId.at("p/b")).target=="p/a");
    const auto captured=zodar::captureScene(world,{});
    CHECK(zodar::parseScene(zodar::serializeScene(captured))==captured);
    zodar::World restored;
    const auto next=zodar::instantiateScene(captured,restored,{});
    CHECK(restored.get<SavedLink>(next.byId.at("p/a")).target=="p/b");
    CHECK_THROWS((void)zodar::parseScene(R"({"version":1,"entities":[{"savedLink":{"unknown":3}}]})"));
    CHECK_THROWS((void)zodar::parseScene(R"({"version":1,"entities":[{"savedLink":{"target":"missing"}}]})"));
    CHECK_THROWS((void)zodar::parseScene(R"({"version":1,"entities":[{"savedLink":{"amount":0}}]})"));
    CHECK_THROWS(zodar::registerSceneComponent<SavedLink>("savedLink",{}));
}
TEST_CASE("D6 save staging: round trip, incompatible/corrupt saves, backups, recovery and failed writes") {
    registerLink(); Temp temp;
    const auto path=temp.path/"slot.json";
    zodar::SaveGameDesc desc{"tests",1,"level","hash",2,1};
    zodar::SaveGameData data;
    data.world=zodar::parseScene(R"({"version":1,"entities":[{"id":"one","savedLink":{"amount":17}}]})");
    data.seed=8; data.rng["stream"]=91; data.tick=3; data.game["state"]="{\"value\":4}";
    zodar::writeSaveGame(path,desc,data);
    const auto first=bytes(path);
    data.tick=4; zodar::writeSaveGame(path,desc,data);
    CHECK(bytes(path.string()+".bak1")==first);
    data.tick=5; zodar::writeSaveGame(path,desc,data);
    CHECK(bytes(path.string()+".bak2")==first);
    auto staged=zodar::loadSaveGame(path,desc,{});
    CHECK(staged.data.tick==5);
    CHECK(staged.world.get<SavedLink>(staged.instance.byId.at("one")).amount==17);
    auto incompatible=desc; incompatible.gameVersion=2;
    CHECK_THROWS(zodar::loadSaveGame(path,incompatible,{}));
    zodar::World live; const auto original=live.create(); live.add<SavedLink>(original,SavedLink{42,{}});
    zodar::writeFileAtomically(path,"corrupt");
    CHECK_THROWS(zodar::loadSaveGame(path,desc,{},false));
    CHECK(live.get<SavedLink>(original).amount==42);
    staged=zodar::loadSaveGame(path,desc,{});
    CHECK(staged.data.tick==4);
    CHECK(staged.recoveredFrom==path.string()+".bak1");
    data.game["state"]="invalid json";
    CHECK_THROWS(zodar::writeSaveGame(path,desc,data));
    CHECK(bytes(path)=="corrupt");
    data.game["state"]="{}";
    std::filesystem::remove(path.string()+".bak1");
    std::filesystem::create_directory(path.string()+".bak1");
    CHECK_THROWS(zodar::writeSaveGame(path,desc,data));
    CHECK(bytes(path)=="corrupt");
    CHECK_THROWS(zodar::loadSaveGame(temp.path/"missing",desc,{}));
    CHECK_FALSE(zodar::autosaveDue(desc,0,9,0.1));
    CHECK(zodar::autosaveDue(desc,9,10,0.1));
}
TEST_CASE("D6 checkpoint state: staged events, named timers, RNG and input continue deterministically") {
    zodar::Gameplay gameplay;
    struct Event { int value; };
    gameplay.events().registerType<Event>("event",[](const Event& e){return std::to_string(e.value);},
        [](std::string_view bytes){return Event{std::stoi(std::string(bytes))};});
    int timerTotal=0;
    gameplay.timers().registerCallback("add",[&](std::string_view s){timerTotal+=std::stoi(std::string(s));});
    gameplay.timers().afterNamed(2,"add","7",2);
    gameplay.events().emit(Event{3});
    gameplay.step(0.1);
    gameplay.events().emit(Event{5});
    const auto snapshot=gameplay.capture();
    CHECK(gameplay.events().read<Event>()[0].value==3);
    auto staged=gameplay.stage(snapshot);
    gameplay.step(0.1); CHECK(timerTotal==7);
    gameplay.commit(std::move(staged)); timerTotal=0;
    CHECK(gameplay.events().read<Event>()[0].value==3);
    gameplay.step(0.1); CHECK(timerTotal==7);
    CHECK(gameplay.events().read<Event>()[0].value==5);
    auto invalid=snapshot; invalid.timers.entries[0].due=0;
    CHECK_THROWS(gameplay.stage(invalid));
    CHECK(gameplay.completedTicks()==2);
    CHECK_THROWS(gameplay.step(0.1,[&]{(void)gameplay.capture();}));
    zodar::Gameplay anonymous; anonymous.timers().after(3,[]{});
    CHECK_THROWS(anonymous.capture());
    zodar::Input input;
    input.setMap(zodar::InputMap::parse(R"({"schema":"zodar.input_map","version":1,"contexts":[{"name":"test","actions":{"jump":{"type":"button","bindings":[]}}}]})"));
    input.inject("jump",{1,0,true,true,false}); input.beginStep();
    auto nextInput=input.stage(input.capture());
    input.beginStep(); nextInput.beginStep();
    CHECK(input.snapshot()==nextInput.snapshot());
    CHECK_THROWS(input.stage("{}"));
    zodar::Random rng(13); (void)rng.next("a");
    zodar::Random next; next.restore(rng.seed(),rng.states());
    CHECK(rng.next("a")==next.next("a"));
}
