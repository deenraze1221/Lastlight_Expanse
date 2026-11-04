#include "Input/Input.h"
#include "Core/Error.h"
#include "SceneFile/DataError.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <limits>
#include <sstream>

namespace zodar {
bool validInputCode(std::string_view source, std::string& code);
using Json = nlohmann::json;
namespace {
void keys(const Json& j, std::initializer_list<std::string_view> allowed) {
    if (!j.is_object()) fail("expected object");
    for (const auto& [k,v] : j.items()) {
        (void)v;
        if (std::find(allowed.begin(), allowed.end(), k) == allowed.end()) fail("unknown field '" + k + "'");
    }
}
bool delta(std::string_view source) { return source == "mouseDelta" || source == "wheel"; }
}
std::string inputHash(std::string_view bytes) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : bytes) { h ^= c; h *= 1099511628211ull; }
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::hex << std::setw(16) << std::setfill('0') << h; return out.str();
}
InputMap InputMap::parse(std::string_view text, std::string_view file) {
    try {
        std::vector<std::set<std::string>> objectKeys;
        const auto root = Json::parse(text,[&](int,Json::parse_event_t event,Json& parsed) {
            if(event==Json::parse_event_t::object_start) objectKeys.emplace_back();
            if(event==Json::parse_event_t::key && !objectKeys.back().insert(parsed.get<std::string>()).second) fail("duplicate input-map field");
            if(event==Json::parse_event_t::object_end) objectKeys.pop_back();
            return true;
        });
        keys(root, {"schema","version","contexts"});
        if (root.at("schema") != "zodar.input_map" || (!root.at("version").is_number_unsigned() || root.at("version") != 1)) fail("unsupported input map schema/version");
        if (!root.at("contexts").is_array()) fail("contexts must be an array");
        InputMap map;
        std::set<std::string> names;
        std::map<std::string,std::string> types;
        for (const auto& c : root.at("contexts")) {
            keys(c, {"name","priority","actions"});
            InputContext ctx; ctx.name=c.at("name").get<std::string>();
            if(c.contains("priority") && (!c.at("priority").is_number_integer() ||
                c.at("priority").get<double>()<std::numeric_limits<int>::min() ||
                c.at("priority").get<double>()>std::numeric_limits<int>::max())) fail("priority must be an integer in range");
            ctx.priority=c.value("priority",0);
            if (ctx.name.empty() || !names.insert(ctx.name).second) fail("empty/duplicate context");
            if (!c.at("actions").is_object()) fail("actions must be an object");
            for (const auto& [name,a] : c.at("actions").items()) {
                keys(a,{"type","bindings"});
                InputAction action; action.type=a.at("type").get<std::string>();
                if (name.empty() || (action.type!="button" && action.type!="axis1" && action.type!="axis2")) fail("invalid action/type");
                if (types.contains(name) && types.at(name)!=action.type) fail("action type differs between contexts");
                types[name]=action.type;
                if (!a.at("bindings").is_array()) fail("bindings must be an array");
                for (const auto& b : a.at("bindings")) {
                    keys(b,{"source","code","scale","deadZone","invert"});
                    InputBinding bind; bind.source=b.at("source"); bind.code=b.at("code");
                    const std::set<std::string> sources{"key","mouseButton","mouseDelta","wheel","gamepadButton","gamepadAxis"};
                    if (!sources.contains(bind.source) || !validInputCode(bind.source,bind.code)) fail("invalid binding source/code");
                    if (b.contains("scale")) {
                        const auto& v=b.at("scale");
                        if (!v.is_array() || v.size()!=2) fail("scale needs [x,y]");
                        bind.x=v[0]; bind.y=v[1];
                    }
                    bind.deadZone=b.value("deadZone",0.0f); bind.invert=b.value("invert",false);
                    if (!std::isfinite(bind.x) || !std::isfinite(bind.y) || !std::isfinite(bind.deadZone) || bind.deadZone<0 || bind.deadZone>=1)
                        fail("invalid binding scale/deadZone");
                    if (action.type!="axis2" && bind.y!=0) fail("only axis2 supports a y scale");
                    if (action.type=="button" && delta(bind.source)) fail("delta bindings require an axis action");
                    action.bindings.push_back(std::move(bind));
                }
                ctx.actions.emplace(name,std::move(action));
            }
            map.contexts.push_back(std::move(ctx));
        }
        return map;
    } catch (const Json::parse_error& e) {
        uint32_t line=1,column=1;
        for (size_t i=0; i<std::min(text.size(),e.byte-1); ++i) { if(text[i]=='\n'){++line;column=1;}else ++column; }
        throw DataError({{std::string(file),line,column,"",e.what()}});
    } catch (const std::exception& e) { throw DataError({{std::string(file),1,1,"",e.what()}}); }
}
InputMap InputMap::load(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) throw DataError({{file.string(),1,1,"","cannot open input map"}});
    return parse(std::string(std::istreambuf_iterator<char>(in),{}),file.string());
}
std::string InputMap::serialize() const {
    Json root={{"schema","zodar.input_map"},{"version",1},{"contexts",Json::array()}};
    for (const auto& c : contexts) {
        Json actions=Json::object();
        for (const auto& [name,a] : c.actions) {
            Json bindings=Json::array();
            for (const auto& b : a.bindings) bindings.push_back({{"source",b.source},{"code",b.code},{"scale",{b.x,b.y}},{"deadZone",b.deadZone},{"invert",b.invert}});
            actions[name]={{"type",a.type},{"bindings",bindings}};
        }
        root["contexts"].push_back({{"name",c.name},{"priority",c.priority},{"actions",actions}});
    }
    return root.dump();
}
std::string InputMap::hash() const { return inputHash(serialize()); }
std::string Input::capture() const {
    const auto actions = [](const ActionSnapshot& state) {
        Json out = Json::object();
        for (const auto& [name, v] : state) out[name] = {v.x,v.y,v.held,v.pressed,v.released};
        return out;
    };
    return Json{{"map",m_map.hash()},{"enabled",m_enabled},{"controls",m_controls},
        {"pending",actions(m_pending)},{"state",actions(m_state)},{"injected",actions(m_injected)},
        {"relativeMouse",m_relativeMouse}}.dump();
}
Input Input::stage(std::string_view bytes) const {
    const auto j = Json::parse(bytes);
    if (!j.is_object() || j.size()!=7 || !j.at("enabled").is_object() || !j.at("controls").is_object() || j.at("map")!=m_map.hash()) fail("incompatible saved input map");
    Input out = *this;
    out.m_enabled.clear();
    for (const auto& [name,v] : j.at("enabled").items()) {
        if (!m_enabled.contains(name) || !v.is_boolean()) fail("invalid saved input context");
        out.m_enabled[name] = v.get<bool>();
    }
    if (out.m_enabled.size()!=m_enabled.size()) fail("missing saved input context");
    out.m_controls.clear();
    for (const auto& [name,v] : j.at("controls").items()) {
        if (!v.is_number() || !std::isfinite(v.get<float>())) fail("invalid saved input control");
        out.m_controls[name]=v.get<float>();
    }
    const auto actions = [&](const Json& values, bool complete) {
        if (!values.is_object()) fail("invalid saved actions");
        ActionSnapshot result;
        for (const auto& [name,v] : values.items()) {
            if (!m_state.contains(name) || !v.is_array() || v.size()!=5 || !v[0].is_number() || !v[1].is_number() ||
                !v[2].is_boolean() || !v[3].is_boolean() || !v[4].is_boolean() ||
                !std::isfinite(v[0].get<float>()) || !std::isfinite(v[1].get<float>())) fail("invalid saved action");
            result[name]={v[0].get<float>(),v[1].get<float>(),v[2].get<bool>(),v[3].get<bool>(),v[4].get<bool>()};
        }
        if (complete && result.size()!=m_state.size()) fail("incomplete saved actions");
        return result;
    };
    out.m_pending=actions(j.at("pending"),true); out.m_state=actions(j.at("state"),true);
    out.m_injected=actions(j.at("injected"),false);
    if (!j.at("relativeMouse").is_boolean()) fail("invalid saved mouse mode");
    out.m_relativeMouse=j.at("relativeMouse").get<bool>();
    return out;
}
void Input::commit(Input&& s) noexcept {
    m_enabled.swap(s.m_enabled); m_controls.swap(s.m_controls); m_pending.swap(s.m_pending);
    m_state.swap(s.m_state); m_injected.swap(s.m_injected); m_relativeMouse=s.m_relativeMouse;
}
void Input::setMap(InputMap map) {
    m_map=std::move(map); m_enabled.clear(); m_controls.clear(); m_pending.clear(); m_state.clear(); m_injected.clear();
    for (const auto& c:m_map.contexts) { m_enabled[c.name]=true; for (const auto& [name,a]:c.actions) { (void)a; m_pending[name]={}; } }
    m_state=m_pending;
}
void Input::setContext(std::string_view name, bool enabled) {
    auto it=m_enabled.find(std::string(name)); if(it==m_enabled.end()) fail("unknown input context");
    it->second=enabled; evaluate();
}
void Input::setRelativeMouse(bool enabled) { m_relativeMouse=enabled; }
void Input::feed(std::string_view source, std::string_view code, float value) {
    if (!std::isfinite(value)) fail("non-finite input");
    const std::string k=std::string(source)+":"+std::string(code);
    const float next = delta(source) && m_controls.contains(k) ? m_controls.at(k) + value : value;
    if (!std::isfinite(next)) fail("input control overflow");
    m_controls[k]=next;
    evaluate();
}
void Input::resetControls() { m_controls.clear(); evaluate(); }
void Input::evaluate() {
    std::vector<const InputContext*> contexts;
    for(const auto& c:m_map.contexts) if(m_enabled.at(c.name)) contexts.push_back(&c);
    std::stable_sort(contexts.begin(),contexts.end(),[](auto a,auto b){return a->priority>b->priority;});
    std::set<std::string> used,claimed;
    ActionSnapshot next;
    for(const auto* c:contexts) {
        std::set<std::string> controls;
        for(const auto& [name,a]:c->actions) {
            if(claimed.contains(name)) continue;
            claimed.insert(name);
            ActionState value;
            for(const auto& b:a.bindings) {
                const std::string k=b.source+":"+b.code;
                controls.insert(k);
                if(used.contains(k)) continue;
                float v=m_controls.contains(k)?m_controls.at(k):0;
                if(std::abs(v)<=b.deadZone) v=0;
                else if(b.deadZone>0) v=std::copysign((std::abs(v)-b.deadZone)/(1-b.deadZone),v);
                if (!std::isfinite(v)) fail("input binding overflow");
                if(b.invert) v=-v;
                if(a.type=="button") value.held |= v*b.x!=0;
                else {
                    value.x+=v*b.x; value.y+=v*b.y;
                    if (!std::isfinite(value.x) || !std::isfinite(value.y)) fail("input action overflow");
                }
            }
            if(a.type=="button") value.x=value.held?1.0f:0.0f;
            next[name]=value;
        }
        used.insert(controls.begin(),controls.end());
    }
    for(auto& [name,old]:m_pending) {
        const auto v=next[name];
        old.pressed |= !old.held && v.held; old.released |= old.held && !v.held;
        old.held=v.held; old.x=v.x; old.y=v.y;
    }
}
void Input::inject(std::string_view actionName, ActionState state) {
    const std::string name(actionName);
    if(!m_state.contains(name) || !std::isfinite(state.x) || !std::isfinite(state.y)) fail("invalid injected action");
    m_injected[name]=state;
}
void Input::beginStep() {
    evaluate();
    auto previous=std::move(m_state);
    m_state=m_pending;
    for(const auto& [name,v]:m_injected) m_state[name]=v;
    for(auto& [name,v]:m_state) {
        const auto& old=previous.at(name);
        v.pressed |= v.held && !old.held;
        v.released |= !v.held && old.held;
    }
    m_injected.clear();
    for(auto& [name,v]:m_pending) { (void)name; v.pressed=false; v.released=false; }
    for(auto& [key,v]:m_controls) if(key.starts_with("mouseDelta:") || key.starts_with("wheel:")) v=0;
}
const ActionState& Input::action(std::string_view name) const {
    const auto it=m_state.find(std::string(name)); if(it==m_state.end()) fail("unknown action '"+std::string(name)+"'");
    return it->second;
}
} // namespace zodar
