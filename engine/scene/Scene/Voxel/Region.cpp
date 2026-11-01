#include "Scene/Voxel/Region.h"
#include "Core/Error.h"
#include "Core/AtomicFile.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace zodar::voxel {
RegionLayout::RegionLayout(float size) {
    const double n = 64.0 / (32.0 * double(size));
    ZODAR_ENSURE(std::isfinite(n) && n >= 1 && n <= 1024 && n == std::floor(n),
                 "region grid must divide 64 metres into integral chunk columns");
    columns = static_cast<int>(n);
}
RegionCoord RegionLayout::regionOf(ChunkCoord c) const {
    const auto divide = [this](int32_t v) { return v / columns - (v % columns < 0 ? 1 : 0); };
    return {divide(c.x), divide(c.z)};
}
} // namespace zodar::voxel

#include <fstream>
#include <charconv>
#include <iomanip>
#include <set>
#include <sstream>

namespace zodar::voxel {
namespace {
constexpr size_t kMaxRegionBytes = 64 * 1024 * 1024;
constexpr size_t kMaxRecords = 1024 * 1024;
void validate(const RegionData& d) {
    RegionLayout layout(d.voxelSize);
    ZODAR_ENSURE(d.minY <= d.maxY && int64_t(d.maxY) - d.minY < 4096, "invalid region height");
    ZODAR_ENSURE(d.baseIdentity.size() <= 4096, "base identity too long");
    size_t total = 0;
    for (const auto* layer : {&d.authored, &d.runtime}) {
        for (const auto& [coord, cells] : *layer) {
            ZODAR_ENSURE(std::abs(int64_t(coord.x))<=31250000 && std::abs(int64_t(coord.y))<=31250000 && std::abs(int64_t(coord.z))<=31250000,"region coordinate overflow");
            ZODAR_ENSURE(layout.regionOf(coord) == d.coord && coord.y >= d.minY && coord.y <= d.maxY,
                         "chunk outside region");
            ZODAR_ENSURE(!cells.empty(), "empty override chunk");
            for (const auto& [i, name] : cells) {
                ZODAR_ENSURE(i < kChunkVolume && name.size() <= 1024, "invalid override cell");
                ZODAR_ENSURE(++total <= kMaxRecords, "region override limit exceeded");
            }
        }
    }
}

}
uint64_t regionMemoryBytes(const RegionData& d) {
    uint64_t bytes=sizeof(d)+d.baseIdentity.capacity();
    for (const auto* layer : {&d.authored,&d.runtime}) for(const auto& [coord,cells]:*layer) {
        bytes+=128;
        for(const auto& [i,name]:cells) bytes+=128+name.capacity();
    }
    return bytes;
}
std::string serializeRegion(const RegionData& d) {
    validate(d);
    std::set<std::string> unique{""};
    for (const auto* layer : {&d.authored, &d.runtime})
        for (const auto& [coord, cells] : *layer)
            for (const auto& [i, name] : cells) unique.insert(name);
    std::vector<std::string> names(unique.begin(), unique.end());
    std::map<std::string, size_t> ids;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "zodar.region 1\n" << d.coord.x << ' ' << d.coord.z << ' '
        << std::setprecision(std::numeric_limits<float>::max_digits10) << d.voxelSize << ' '
        << d.minY << ' ' << d.maxY << ' ' << std::quoted(d.baseIdentity) << '\n' << names.size() << '\n';
    for (size_t i = 0; i < names.size(); ++i) { ids[names[i]] = i; out << std::quoted(names[i]) << '\n'; }
    for (const auto* layer : {&d.authored, &d.runtime}) {
        out << layer->size() << '\n';
        for (const auto& [coord, cells] : *layer) {
            std::set<size_t> used;
            for(const auto& [i,name]:cells) used.insert(ids.at(name));
            std::vector<size_t> palette(used.begin(),used.end());
            std::map<size_t,size_t> local;
            for(size_t i=0;i<palette.size();++i) local[palette[i]]=i;
            struct Run { uint32_t first, length; size_t name; };
            std::vector<Run> runs;
            for (const auto& [i, name] : cells) {
                if (!runs.empty() && runs.back().first + runs.back().length == i && runs.back().name == local.at(ids.at(name)))
                    ++runs.back().length;
                else runs.push_back({i, 1, local.at(ids.at(name))});
            }
            out << coord.x << ' ' << coord.y << ' ' << coord.z << ' ' << palette.size();
            for(auto id:palette) out<<' '<<id;
            out<<' '<<runs.size()<<'\n';
            for (const auto& r : runs) out << r.first << ' ' << r.length << ' ' << r.name << '\n';
        }
    }
    auto bytes = out.str();
    ZODAR_ENSURE(bytes.size() <= kMaxRegionBytes, "region file too large");
    return bytes;
}
namespace {
// Conservative managed-allocation charges. Failure occurs before the allocation, including
// input storage, container nodes and temporary palette metadata. Freed scratch remains charged
// until parsing ends, so this is an upper bound rather than a promise to accept every file.
class DecodeBudget {
public:
    explicit DecodeBudget(uint64_t limit) : remaining(limit) {}
    void charge(uint64_t bytes) {
        ZODAR_ENSURE(bytes<=remaining,"region decode memory budget exceeded");
        remaining-=bytes;
    }
private:
    uint64_t remaining;
};
class RegionReader {
public:
    RegionReader(std::string_view bytes,DecodeBudget& budget) : input(bytes), memory(budget) {}
    static bool space(char c) { return c==' ' || c=='\n' || c=='\r' || c=='\t' || c=='\f' || c=='\v'; }
    void whitespace() { while(position<input.size() && space(input[position])) ++position; }
    std::string_view token() {
        whitespace(); const auto first=position;
        while(position<input.size() && !space(input[position])) ++position;
        ZODAR_ENSURE(position>first,"missing region token");
        return input.substr(first,position-first);
    }
    template<class T> T number() {
        const auto text=token(); T value{};
        const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
        ZODAR_ENSURE(error==std::errc{} && end==text.data()+text.size(),"invalid region number");
        return value;
    }
    size_t count(size_t max) {
        const auto value=number<uint64_t>(); ZODAR_ENSURE(value<=max,"invalid region count");
        return static_cast<size_t>(value);
    }
    std::string quoted(size_t maxLength) {
        whitespace(); ZODAR_ENSURE(position<input.size() && input[position]=='"',"expected quoted region string");
        const auto first=++position; size_t length=0;
        while(position<input.size() && input[position]!='"') {
            if(input[position]=='\\') { ++position; ZODAR_ENSURE(position<input.size(),"truncated region escape"); }
            ++position; ZODAR_ENSURE(++length<=maxLength,"region string too long");
        }
        ZODAR_ENSURE(position<input.size(),"unterminated region string");
        memory.charge(2*(length+1)+sizeof(std::string));
        std::string result; result.reserve(length);
        for(auto i=first;i<position;++i) { if(input[i]=='\\') ++i; result.push_back(input[i]); }
        ++position; return result;
    }
    bool done() { whitespace(); return position==input.size(); }
private:
    std::string_view input;
    DecodeBudget& memory;
    size_t position=0;
};
}
RegionData parseRegion(std::string_view bytes, uint64_t maxMemory) {
    ZODAR_ENSURE(bytes.size()<=kMaxRegionBytes,"region file too large");
    DecodeBudget budget(maxMemory);
    budget.charge(bytes.size()+1); // input is borrowed, but belongs to the decode's live footprint
    budget.charge(sizeof(RegionData));
    RegionReader in(bytes,budget);
    ZODAR_ENSURE(in.token()=="zodar.region" && in.number<int>()==1,"unsupported region schema");
    RegionData d;
    d.coord={in.number<int32_t>(),in.number<int32_t>()}; d.voxelSize=in.number<float>();
    d.minY=in.number<int32_t>(); d.maxY=in.number<int32_t>(); d.baseIdentity=in.quoted(4096);
    validate(d); // reject incompatible dimensions before allocating metadata
    RegionLayout layout(d.voxelSize);
    const auto n=in.count(65535); ZODAR_ENSURE(n>0,"region requires air palette entry");
    budget.charge(n*sizeof(std::string));
    std::vector<std::string> names(n);
    std::set<std::string_view> unique;
    for(auto& name:names) {
        name=in.quoted(1024);
        budget.charge(128+sizeof(std::string_view));
        ZODAR_ENSURE(unique.insert(name).second,"duplicate region palette entry");
    }
    ZODAR_ENSURE(names.front().empty(),"region palette zero must be air");
    size_t total=0;
    for(auto* layer:{&d.authored,&d.runtime}) {
        const auto chunks=in.count(kMaxRecords);
        for(size_t c=0;c<chunks;++c) {
            ChunkCoord coord{in.number<int32_t>(),in.number<int32_t>(),in.number<int32_t>()};
            ZODAR_ENSURE(std::abs(int64_t(coord.x))<=31250000 && std::abs(int64_t(coord.y))<=31250000 && std::abs(int64_t(coord.z))<=31250000,"region coordinate overflow");
            ZODAR_ENSURE(layout.regionOf(coord)==d.coord && coord.y>=d.minY && coord.y<=d.maxY,"chunk outside region");
            budget.charge(128+sizeof(RegionLayer::value_type));
            auto [it,inserted]=layer->try_emplace(coord); ZODAR_ENSURE(inserted,"duplicate region chunk");
            const auto paletteSize=in.count(n); ZODAR_ENSURE(paletteSize>0,"empty chunk palette");
            budget.charge(2*paletteSize*sizeof(size_t));
            std::vector<size_t> palette(paletteSize);
            for(auto& id:palette) id=in.count(n-1);
            auto sorted=palette; std::sort(sorted.begin(),sorted.end());
            ZODAR_ENSURE(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end(),"duplicate chunk palette entry");
            const auto runs=in.count(kChunkVolume); ZODAR_ENSURE(runs>0,"empty override chunk");
            size_t end=0;
            for(size_t r=0;r<runs;++r) {
                const auto first=in.count(kChunkVolume-1), length=in.count(kChunkVolume);
                const auto id=palette[in.count(palette.size()-1)];
                ZODAR_ENSURE(length>0 && first>=end && first+length<=kChunkVolume,"invalid or overlapping region run");
                total+=length; ZODAR_ENSURE(total<=kMaxRecords,"region override limit exceeded");
                budget.charge(length*(128+sizeof(NamedCells::value_type)+2*(names[id].size()+1)));
                end=first+length;
                for(size_t i=first;i<end;++i) it->second.emplace(static_cast<uint32_t>(i),names[id]);
            }
        }
    }
    ZODAR_ENSURE(in.done(),"trailing region data");
    return d;
}
RegionData loadRegion(const std::filesystem::path& path, uint64_t maxMemory) {
    constexpr uint64_t ioReservation=16*1024; // file stream buffer/metadata stays live during parsing
    const auto size=std::filesystem::file_size(path);
    ZODAR_ENSURE(size<=kMaxRegionBytes,"region file too large");
    DecodeBudget budget(maxMemory);
    budget.charge(ioReservation); budget.charge(size+1); budget.charge(sizeof(RegionData));
    std::string bytes(static_cast<size_t>(size),'\0');
    std::ifstream in(path,std::ios::binary); ZODAR_ENSURE(bool(in),"cannot open region");
    in.read(bytes.data(),static_cast<std::streamsize>(size));
    ZODAR_ENSURE(in.gcount()==static_cast<std::streamsize>(size) && in.peek()==std::char_traits<char>::eof() && !in.bad(),"region changed or failed while reading");
    return parseRegion(bytes,maxMemory-ioReservation);
}
void saveRegion(const std::filesystem::path& path, const RegionData& d) {
    const auto bytes = serializeRegion(d); // validate before touching the old file
    zodar::writeFileAtomically(path,bytes);
}
void applyRegionLayer(const RegionLayer& layer, ChunkCoord coord, VoxelChunk& chunk, const BlockNames& names) {
    auto it = layer.find(coord);
    if (it == layer.end()) return;
    ZODAR_ENSURE(!names.empty() && names.front().empty() && names.size() <= 65535, "invalid block names");
    std::map<std::string, Voxel> ids;
    for (size_t i = 0; i < names.size(); ++i)
        ZODAR_ENSURE(ids.emplace(names[i], static_cast<Voxel>(i)).second, "duplicate block name");
    std::vector<std::pair<uint32_t, Voxel>> resolved;
    for (const auto& [i, name] : it->second) {
        ZODAR_ENSURE(i < kChunkVolume && ids.contains(name), "unknown region block name or index");
        resolved.emplace_back(i, ids.at(name));
    }
    for (const auto& [i, id] : resolved) chunk.set(int(i & 31), int(i >> 10), int((i >> 5) & 31), id);
}
} // namespace zodar::voxel
