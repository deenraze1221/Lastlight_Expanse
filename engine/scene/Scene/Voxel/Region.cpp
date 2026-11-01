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
