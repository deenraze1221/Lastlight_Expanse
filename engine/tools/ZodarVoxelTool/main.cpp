#include "Scene/Voxel/Structure.h"
#include "SceneFile/BlockFile.h"
#include "Core/Error.h"
#include <charconv>
#include <fstream>
#include <iostream>
using namespace zodar::voxel;
namespace {
std::string read(const std::filesystem::path& p) {
    ZODAR_ENSURE(std::filesystem::file_size(p)<=64*1024*1024,"input too large");
    std::ifstream in(p,std::ios::binary); ZODAR_ENSURE(bool(in),"cannot open input");
    return std::string((std::istreambuf_iterator<char>(in)),{});
}
int number(const char* text) {
    int value=0; const std::string_view s(text);
    auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),value);
    ZODAR_ENSURE(error==std::errc{} && end==s.data()+s.size(),"invalid integer"); return value;
}
void write(const std::filesystem::path& p,const std::string& bytes) {
    auto tmp=p; tmp+=".tmp";
    try {
        std::ofstream out(tmp,std::ios::binary|std::ios::trunc); out<<bytes; out.close();
        ZODAR_ENSURE(bool(out),"cannot write structure"); std::filesystem::rename(tmp,p);
    } catch(...) { std::error_code ec; std::filesystem::remove(tmp,ec); throw; }
}
}
int main(int argc,char** argv) {
    try {
        ZODAR_ENSURE(argc>=3,"usage: ZodarVoxelTool info FILE | capture REGION OUT X Y Z SX SY SZ | stamp REGION STRUCT OUT X Y Z YAW BLOCKS");
        const std::string action=argv[1];
        if(action=="info" && argc==3) {
            const auto bytes=read(argv[2]);
            if(bytes.starts_with("zodar.region")) {
                const auto d=parseRegion(bytes);
                std::cout<<"region "<<d.coord.x<<' '<<d.coord.z<<", authored chunks "<<d.authored.size()<<", runtime chunks "<<d.runtime.size()<<", memory "<<regionMemoryBytes(d)<<'\n';
            } else { const auto s=parseStructure(bytes); std::cout<<"structure "<<s.size.x<<'x'<<s.size.y<<'x'<<s.size.z<<", cells "<<s.cells.size()<<'\n'; }
        } else if(action=="capture" && argc==10) {
            // Disk regions contain differences, not the base terrain. Capture known overrides only.
            const auto d=loadRegion(argv[2]); const glm::ivec3 min(number(argv[4]),number(argv[5]),number(argv[6]));
            for(int a=0;a<3;++a) ZODAR_ENSURE(int64_t(min[a])>=-1000000000 && int64_t(min[a])<=1000000000,"invalid capture origin");
            Structure s; s.size={number(argv[7]),number(argv[8]),number(argv[9])};
            for(const auto* layer:{&d.authored,&d.runtime}) for(const auto& [c,cells]:*layer) for(const auto& [i,name]:cells) {
                const auto p=c.originVoxel()+glm::ivec3(int(i&31),int(i>>10),int((i>>5)&31))-min;
                if(glm::all(glm::greaterThanEqual(p,glm::ivec3(0))) && glm::all(glm::lessThan(p,s.size))) s.cells[{p.x,p.y,p.z}]=name;
            }
            write(argv[3],serializeStructure(s)); std::cout<<"captured "<<s.cells.size()<<" stored overrides (inherited cells omitted)\n";
        } else if(action=="stamp" && argc==10) {
            auto d=loadRegion(argv[2]); const auto s=parseStructure(read(argv[3]));
            const auto yaw=number(argv[8]); ZODAR_ENSURE(yaw>=0 && yaw<4,"invalid yaw");
            const auto catalogue=zodar::loadBlockCatalogue(argv[9]);
            stampStructure(d,rotateStructure(s,static_cast<uint8_t>(yaw),catalogue.names),{number(argv[5]),number(argv[6]),number(argv[7])});
            saveRegion(argv[4],d);
        } else ZODAR_ENSURE(false,"invalid voxel command or argument count");
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
