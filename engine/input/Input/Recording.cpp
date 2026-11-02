#include "Input/Recording.h"
#include "Core/AtomicFile.h"
#include "Core/Error.h"
#include "SceneFile/DataError.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <sstream>
#include <set>
#include <algorithm>

namespace zodar {
using Json=nlohmann::json;
namespace {
void keys(const Json& j, std::initializer_list<std::string_view> fields) {
    if(!j.is_object() || j.size()!=fields.size()) fail("missing/extra recording fields");
    for(auto f:fields) if(!j.contains(f)) fail("missing recording field '"+std::string(f)+"'");
}
void unsignedInteger(const Json& v) { if(!v.is_number_unsigned()) fail("expected unsigned integer"); }
Json stateJson(const ActionState& s) { return Json::array({s.x,s.y,s.held,s.pressed,s.released}); }
ActionState readState(const Json& j) {
    if(!j.is_array() || j.size()!=5 || !j[0].is_number() || !j[1].is_number() || !j[2].is_boolean() || !j[3].is_boolean() || !j[4].is_boolean()) fail("invalid action state");
    ActionState s{j[0].get<float>(),j[1].get<float>(),j[2].get<bool>(),j[3].get<bool>(),j[4].get<bool>()};
    if(!std::isfinite(s.x)||!std::isfinite(s.y)) fail("non-finite action state");
    return s;
}
}
std::string InputRecording::serialize() const {
    Json head={{"schema","zodar.input_record"},{"version",1},{"app",header.app},{"inputMapHash",header.inputMapHash},
               {"levelHash",header.levelHash},{"seed",header.seed},{"fixedDt",header.fixedDt}};
    std::string out=head.dump()+"\n";
    ActionSnapshot previous;
    uint64_t tick=0;
    for(const auto& t:ticks) {
        if(tick && (t.actions.size()!=previous.size() || std::any_of(previous.begin(),previous.end(),[&](const auto& a){return !t.actions.contains(a.first);})))
            fail("recorded action set changed");
        Json changes=Json::object();
        for(const auto& [name,state]:t.actions) if(!previous.contains(name)||previous.at(name)!=state) changes[name]=stateJson(state);
        out+=Json({{"tick",++tick},{"actions",changes},{"state",t.state}}).dump()+"\n";
        previous=t.actions;
    }
    out+=Json({{"end",tick},{"hash",inputHash(out)}}).dump()+"\n";
    return out;
}
InputRecording InputRecording::parse(std::string_view bytes, std::string_view file) {
    uint32_t line=0;
    try {
        if(bytes.empty() || bytes.back()!='\n') fail("truncated recording (missing final newline)");
        std::istringstream in{std::string(bytes)};
        std::string text;
        InputRecording result;
        ActionSnapshot current;
        size_t offset=0;
        bool ended=false;
        while(std::getline(in,text)) {
            ++line;
            // Reject duplicate object fields rather than accepting last-wins corruption.
            std::vector<std::set<std::string>> objectKeys;
            auto j=Json::parse(text,[&](int,Json::parse_event_t event,Json& parsed) {
                if(event==Json::parse_event_t::object_start) objectKeys.emplace_back();
                if(event==Json::parse_event_t::key && !objectKeys.back().insert(parsed.get<std::string>()).second) fail("duplicate recording field");
                if(event==Json::parse_event_t::object_end) objectKeys.pop_back();
                return true;
            });
            if(ended) fail("data after recording footer");
            if(line==1) {
                keys(j,{"schema","version","app","inputMapHash","levelHash","seed","fixedDt"});
                if(j.at("schema")!="zodar.input_record" || (!j.at("version").is_number_unsigned() || j.at("version")!=1)) fail("unsupported recording schema/version");
                unsignedInteger(j.at("seed"));
                result.header={j.at("app"),j.at("inputMapHash"),j.at("levelHash"),j.at("seed").get<uint64_t>(),j.at("fixedDt").get<double>()};
                if(result.header.app.empty() || result.header.inputMapHash.empty() || !std::isfinite(result.header.fixedDt) || result.header.fixedDt<=0 || result.header.fixedDt>1) fail("invalid recording header");
            } else if(j.contains("end")) {
                keys(j,{"end","hash"}); unsignedInteger(j.at("end"));
                if(j.at("end").get<uint64_t>()!=result.ticks.size() || j.at("hash")!=inputHash(bytes.substr(0,offset))) fail("recording footer count/checksum mismatch");
                ended=true;
            } else {
                keys(j,{"tick","actions","state"}); unsignedInteger(j.at("tick"));
                if(j.at("tick").get<uint64_t>()!=result.ticks.size()+1 || !j.at("actions").is_object() || !j.at("state").is_string()) fail("invalid recording tick");
                for(const auto& [name,value]:j.at("actions").items()) {
                    if(name.empty() || (!result.ticks.empty()&&!current.contains(name))) fail("unknown recorded action");
                    current[name]=readState(value);
                }
                result.ticks.push_back({current,j.at("state").get<std::string>()});
            }
            offset+=text.size()+1;
        }
        if(!ended || result.ticks.empty()) fail("truncated/empty recording");
        return result;
    } catch(const Json::parse_error& e) { throw DataError({{std::string(file),line,static_cast<uint32_t>(e.byte),"",e.what()}}); }
      catch(const std::exception& e) { throw DataError({{std::string(file),line,1,"",e.what()}}); }
}
InputRecording InputRecording::load(const std::filesystem::path& file) {
    std::ifstream in(file,std::ios::binary);
    if(!in) fail("cannot open recording '"+file.string()+"'");
    return parse(std::string(std::istreambuf_iterator<char>(in),{}),file.string());
}
void InputRecording::save(const std::filesystem::path& file) const {
    const auto bytes=serialize();
    (void)parse(bytes,file.string()); // never publish an invalid/incomplete recording
    writeFileAtomically(file,bytes);
}
void InputRecording::checkCompatible(const RecordingHeader& expected, bool lenientHashes) const {
    if(header.app!=expected.app || header.seed!=expected.seed || header.fixedDt!=expected.fixedDt) fail("incompatible recording app/seed/fixedDt");
    if(!lenientHashes && (header.inputMapHash!=expected.inputMapHash || header.levelHash!=expected.levelHash)) fail("incompatible recording input-map/level hash");
}
} // namespace zodar
