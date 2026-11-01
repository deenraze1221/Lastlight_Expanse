#include "Scene/Voxel/Structure.h"
#include "Core/Error.h"
#include <cmath>
#include <iomanip>
#include <sstream>
namespace zodar::voxel {
namespace {
void check(const Structure& s) {
    int64_t volume = 1;
    for (int a = 0; a < 3; ++a) {
        ZODAR_ENSURE(s.size[a] > 0 && s.size[a] <= 1024, "invalid structure size");
        volume *= s.size[a];
    }
    ZODAR_ENSURE(volume <= 1048576 && s.cells.size() <= static_cast<size_t>(volume), "structure too large");
    for (const auto& [p, name] : s.cells)
        ZODAR_ENSURE(p.x >= 0 && p.x < s.size.x && p.y >= 0 && p.y < s.size.y && p.z >= 0 && p.z < s.size.z
                     && name.size() <= 1024, "invalid structure cell");
    ZODAR_ENSURE(s.anchors.size() <= 4096 && s.models.size() <= 4096, "too many structure attachments");
    std::map<std::string, bool> anchors;
    for (const auto& a : s.anchors)
        ZODAR_ENSURE(!a.name.empty() && a.name.size() <= 1024 && anchors.emplace(a.name, true).second,
                     "invalid or duplicate structure anchor");
    for (const auto& m : s.models)
        ZODAR_ENSURE(!m.asset.empty() && m.asset.size() <= 1024 && m.yaw < 4 &&
                     std::isfinite(m.position.x) && std::isfinite(m.position.y) && std::isfinite(m.position.z),
                     "invalid structure model");
}
std::string rotatedName(std::string name, uint8_t yaw) {
    if (name.empty() || yaw == 0) return name;
    unsigned old = 0;
    const auto at = name.find('@');
    if (at != std::string::npos) {
        ZODAR_ENSURE(at + 2 == name.size() && name.back() >= '1' && name.back() <= '3', "invalid rotation name");
        old = unsigned(name.back() - '0'); name.resize(at);
    }
    const auto r = (old + yaw) % 4;
    return r == 0 ? name : name + "@" + std::to_string(r);
}
}
Structure rotateStructure(const Structure& source, uint8_t yaw) {
    check(source); ZODAR_ENSURE(yaw < 4, "invalid structure yaw");
    Structure out = source;
    for (uint8_t r = 0; r < yaw; ++r) {
        const int width = out.size.x;
        auto cell = [width](glm::ivec3 p) { return glm::ivec3(p.z, p.y, width - 1 - p.x); };
        std::map<ChunkCoord, std::string> next;
        for (const auto& [p, name] : out.cells) {
            const auto q = cell({p.x,p.y,p.z}); next[{q.x,q.y,q.z}] = name;
        }
        out.cells = std::move(next);
        out.origin = cell(out.origin);
        for (auto& a : out.anchors) a.cell = cell(a.cell);
        for (auto& m : out.models) {
            m.position = {m.position.z, m.position.y, float(width) - m.position.x};
            m.yaw = uint8_t((m.yaw + 1) % 4);
        }
        std::swap(out.size.x, out.size.z);
    }
    for (auto& [p, name] : out.cells) name = rotatedName(name, yaw);
    return out;
}
Structure captureStructure(const VoxelWorld& world, glm::ivec3 min, glm::ivec3 size, const BlockNames& names) {
    Structure out; out.size = size; check(out);
    for (int y=0; y<size.y; ++y) for (int z=0; z<size.z; ++z) for (int x=0; x<size.x; ++x) {
        const auto p = min + glm::ivec3(x,y,z);
        std::vector<SolidBox> boxes;
        const auto pos = glm::vec3(p) * world.settings().voxelSize;
        ZODAR_ENSURE(world.collectSolidBoxes({pos, pos}, boxes) == QueryStatus::Complete, "capture touches missing chunks");
        const auto id = world.getVoxel(p);
        ZODAR_ENSURE(id < names.size(), "capture block has no stable name");
        out.cells[{x,y,z}] = names[id];
    }
    return out;
}
void stampStructure(RegionData& region, const Structure& s, glm::ivec3 position, bool runtime) {
    check(s); RegionLayout layout(region.voxelSize);
    auto& layer = runtime ? region.runtime : region.authored;
    for (const auto& [p, name] : s.cells) {
        const auto v = position + glm::ivec3(p.x,p.y,p.z) - s.origin;
        const auto coord = chunkOf(v);
        if (layout.regionOf(coord) != region.coord || coord.y < region.minY || coord.y > region.maxY) continue;
        const auto l = localOf(v);
        layer[coord][static_cast<uint32_t>(VoxelChunk::index(l.x,l.y,l.z))] = name;
    }
}
std::string serializeStructure(const Structure& s) {
    check(s); std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(9) << "zodar.structure 1\n" << s.size.x << ' ' << s.size.y << ' ' << s.size.z << ' '
        << s.origin.x << ' ' << s.origin.y << ' ' << s.origin.z << '\n' << s.cells.size() << '\n';
    for (const auto& [p,n] : s.cells) out << p.x << ' ' << p.y << ' ' << p.z << ' ' << std::quoted(n) << '\n';
    out << s.anchors.size() << '\n';
    for (const auto& a : s.anchors) out << std::quoted(a.name) << ' ' << a.cell.x << ' ' << a.cell.y << ' ' << a.cell.z << '\n';
    out << s.models.size() << '\n';
    for (const auto& m : s.models) out << std::quoted(m.asset) << ' ' << m.position.x << ' ' << m.position.y << ' ' << m.position.z << ' ' << unsigned(m.yaw) << '\n';
    return out.str();
}
Structure parseStructure(std::string_view bytes) {
    ZODAR_ENSURE(bytes.size() <= 64*1024*1024, "structure file too large");
    std::istringstream in{std::string(bytes)}; in.imbue(std::locale::classic());
    std::string schema; int version=0; Structure s;
    ZODAR_ENSURE(bool(in >> schema >> version) && schema == "zodar.structure" && version == 1, "unsupported structure");
    ZODAR_ENSURE(bool(in >> s.size.x >> s.size.y >> s.size.z >> s.origin.x >> s.origin.y >> s.origin.z), "bad structure header");
    auto count = [&in](size_t max) { size_t n=0; ZODAR_ENSURE(bool(in >> n) && n <= max, "bad structure count"); return n; };
    const auto cells = count(1048576);
    for (size_t i=0; i<cells; ++i) {
        ChunkCoord p; std::string name;
        ZODAR_ENSURE(bool(in >> p.x >> p.y >> p.z >> std::quoted(name)) && s.cells.emplace(p,name).second, "bad structure cell");
    }
    const auto anchors = count(4096);
    for (size_t i=0; i<anchors; ++i) { StructureAnchor a; ZODAR_ENSURE(bool(in >> std::quoted(a.name) >> a.cell.x >> a.cell.y >> a.cell.z), "bad anchor"); s.anchors.push_back(a); }
    const auto models = count(4096);
    for (size_t i=0; i<models; ++i) { StructureModel m; unsigned yaw=0; ZODAR_ENSURE(bool(in >> std::quoted(m.asset) >> m.position.x >> m.position.y >> m.position.z >> yaw) && yaw<4, "bad model"); m.yaw=uint8_t(yaw); s.models.push_back(m); }
    in >> std::ws; ZODAR_ENSURE(in.eof(), "trailing structure data"); check(s); return s;
}
LayeredGenerator::LayeredGenerator(std::shared_ptr<const IVoxelGenerator> base, BlockNames names,
                                 std::vector<StructureStamp> stamps, RegionLayer authored, RegionLayer runtime)
    : m_base(std::move(base)), m_names(std::move(names)), m_authored(std::move(authored)), m_runtime(std::move(runtime)) {
    ZODAR_ENSURE(m_base, "layered generator requires base");
    for (const auto& stamp : stamps) {
        check(stamp.structure);
        for (const auto& [p,n] : stamp.structure.cells) {
            const auto v = stamp.position + glm::ivec3(p.x,p.y,p.z) - stamp.structure.origin;
            const auto l = localOf(v);
            m_structures[chunkOf(v)][static_cast<uint32_t>(VoxelChunk::index(l.x,l.y,l.z))] = n;
        }
    }
}
void LayeredGenerator::generate(const ChunkCoord& coord, VoxelChunk& out) const {
    m_base->generate(coord, out);
    for (const auto* layer : {&m_structures, &m_authored, &m_runtime}) applyRegionLayer(*layer, coord, out, m_names);
    out.compact();
}
} // namespace zodar::voxel
