#include "Scene/Voxel/Region.h"
#include "Core/Error.h"
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
size_t count(std::istream& in, size_t max) {
    uint64_t n = 0;
    ZODAR_ENSURE(bool(in >> n) && n <= max, "invalid region count");
    return static_cast<size_t>(n);
}
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
            struct Run { uint32_t first, length; size_t name; };
            std::vector<Run> runs;
            for (const auto& [i, name] : cells) {
                if (!runs.empty() && runs.back().first + runs.back().length == i && runs.back().name == ids.at(name))
                    ++runs.back().length;
                else runs.push_back({i, 1, ids.at(name)});
            }
            out << coord.x << ' ' << coord.y << ' ' << coord.z << ' ' << runs.size() << '\n';
            for (const auto& r : runs) out << r.first << ' ' << r.length << ' ' << r.name << '\n';
        }
    }
    auto bytes = out.str();
    ZODAR_ENSURE(bytes.size() <= kMaxRegionBytes, "region file too large");
    return bytes;
}
RegionData parseRegion(std::string_view bytes) {
    ZODAR_ENSURE(bytes.size() <= kMaxRegionBytes, "region file too large");
    std::istringstream in{std::string(bytes)};
    in.imbue(std::locale::classic());
    std::string schema; int version = 0;
    ZODAR_ENSURE(bool(in >> schema >> version) && schema == "zodar.region" && version == 1, "unsupported region schema");
    RegionData d;
    ZODAR_ENSURE(bool(in >> d.coord.x >> d.coord.z >> d.voxelSize >> d.minY >> d.maxY >> std::quoted(d.baseIdentity)),
                 "invalid region header");
    const auto n = count(in, 65535);
    ZODAR_ENSURE(n > 0, "region requires air palette entry");
    std::vector<std::string> names(n);
    std::set<std::string> unique;
    for (auto& name : names) {
        ZODAR_ENSURE(bool(in >> std::quoted(name)) && name.size() <= 1024 && unique.insert(name).second,
                     "invalid region palette");
    }
    ZODAR_ENSURE(names.front().empty(), "region palette zero must be air");
    size_t total = 0;
    for (auto* layer : {&d.authored, &d.runtime}) {
        const auto chunks = count(in, kMaxRecords);
        for (size_t c = 0; c < chunks; ++c) {
            ChunkCoord coord;
            ZODAR_ENSURE(bool(in >> coord.x >> coord.y >> coord.z), "invalid chunk coordinate");
            auto [it, inserted] = layer->try_emplace(coord);
            ZODAR_ENSURE(inserted, "duplicate region chunk");
            const auto runs = count(in, kChunkVolume);
            uint64_t end = 0;
            for (size_t r = 0; r < runs; ++r) {
                const auto first = count(in, kChunkVolume - 1);
                const auto length = count(in, kChunkVolume);
                const auto id = count(in, names.size() - 1);
                ZODAR_ENSURE(length > 0 && first >= end && first + length <= kChunkVolume,
                             "invalid or overlapping region run");
                total += length;
                ZODAR_ENSURE(total <= kMaxRecords, "region override limit exceeded");
                end = first + length;
                for (size_t i = first; i < end; ++i) it->second.emplace(static_cast<uint32_t>(i), names[id]);
            }
        }
    }
    in >> std::ws;
    ZODAR_ENSURE(in.eof(), "trailing region data");
    validate(d);
    return d;
}
RegionData loadRegion(const std::filesystem::path& path) {
    ZODAR_ENSURE(std::filesystem::file_size(path) <= kMaxRegionBytes, "region file too large");
    std::ifstream in(path, std::ios::binary);
    ZODAR_ENSURE(bool(in), "cannot open region");
    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    ZODAR_ENSURE(!in.bad(), "cannot read region");
    return parseRegion(bytes);
}
void saveRegion(const std::filesystem::path& path, const RegionData& d) {
    const auto bytes = serializeRegion(d); // validate before touching the old file
    auto temporary = path; temporary += ".tmp";
    // A RegionStreamer serializes saves for each path; concurrent external writers unsupported.
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        ZODAR_ENSURE(bool(out), "cannot create region temporary file");
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        ZODAR_ENSURE(bool(out), "cannot write region temporary file");
        std::filesystem::rename(temporary, path);
    } catch (...) { std::error_code ec; std::filesystem::remove(temporary, ec); throw; }
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
