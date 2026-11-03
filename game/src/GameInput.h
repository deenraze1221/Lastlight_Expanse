#pragma once
#include "Input/Input.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace lastlight {
// Exact simulation values, independent of locale and decimal rounding.
inline std::string playerFingerprint(const glm::vec3& position, float yaw, float pitch,
                                     const glm::vec3& velocity = {}, bool grounded = false) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::hexfloat << position.x << ',' << position.y << ',' << position.z << ',' << yaw << ',' << pitch
        << ',' << velocity.x << ',' << velocity.y << ',' << velocity.z << ',' << grounded;
    return out.str();
}
inline std::string bunkerContentHash(const std::filesystem::path& assets) {
    std::vector<std::filesystem::path> paths;
    for(std::filesystem::recursive_directory_iterator it(assets),end;it!=end;++it) {
        if(it->is_directory() && (it->path().filename()=="sources" || it->path().extension()==".zodar")) { it.disable_recursion_pending(); continue; }
        if(it->is_regular_file() && it->path().extension()!=".md") paths.push_back(it->path());
    }
    std::sort(paths.begin(),paths.end());
    std::string manifest;
    for(const auto& p:paths) {
        std::ifstream in(p,std::ios::binary);
        if(!in) throw std::runtime_error("cannot hash bunker content: "+p.string());
        const std::string bytes(std::istreambuf_iterator<char>(in),{});
        manifest+=p.lexically_relative(assets).generic_string()+":"+zodar::inputHash(bytes)+"\n";
    }
    return zodar::inputHash(manifest);
}
} // namespace lastlight
