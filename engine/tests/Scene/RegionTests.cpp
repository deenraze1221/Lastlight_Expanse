#include "Scene/Voxel/Region.h"
#include "Core/Error.h"
#include <doctest/doctest.h>
using namespace zodar::voxel;
TEST_CASE("region: metric layout and negative boundaries") {
    RegionLayout half(0.5f);
    CHECK(half.columns == 4);
    CHECK(half.regionOf({3, 50, 3}) == RegionCoord{0, 0});
    CHECK(half.regionOf({-1, 0, -4}) == RegionCoord{-1, -1});
    CHECK(half.regionOf({-5, 0, 4}) == RegionCoord{-2, 1});
    CHECK(RegionLayout(1).columns == 2);
    CHECK_THROWS_AS(RegionLayout(0.3f), zodar::Error);
    CHECK_THROWS_AS(RegionLayout(0), zodar::Error);
}
