#include <doctest/doctest.h>

#include "Renderer/Vulkan/PipelineCache.h"

#include <cstring>
#include <vector>

using namespace raven::gfx;

namespace {

std::vector<uint8_t> blob(uint32_t vendor, uint32_t device, uint8_t uuidByte, size_t extra = 64) {
    VkPipelineCacheHeaderVersionOne h{};
    h.headerSize = sizeof(h);
    h.headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
    h.vendorID = vendor;
    h.deviceID = device;
    std::memset(h.pipelineCacheUUID, uuidByte, VK_UUID_SIZE);
    std::vector<uint8_t> data(sizeof(h) + extra, 0xCD);
    std::memcpy(data.data(), &h, sizeof(h));
    return data;
}

} // namespace

TEST_CASE("pipeline cache: header validation") {
    std::array<uint8_t, VK_UUID_SIZE> uuid{};
    uuid.fill(7);
    std::string reason;
    CHECK(isPipelineCacheCompatible(blob(0x10DE, 0x2786, 7), 0x10DE, 0x2786, uuid, &reason));
    CHECK_FALSE(isPipelineCacheCompatible(blob(0x1002, 0x2786, 7), 0x10DE, 0x2786, uuid, &reason));
    CHECK(reason == "different GPU vendor");
    CHECK_FALSE(isPipelineCacheCompatible(blob(0x10DE, 0x1111, 7), 0x10DE, 0x2786, uuid, &reason));
    CHECK(reason == "different GPU");
    CHECK_FALSE(isPipelineCacheCompatible(blob(0x10DE, 0x2786, 8), 0x10DE, 0x2786, uuid, &reason));
    CHECK(reason == "different driver version");
    const std::vector<uint8_t> tiny(8, 0);
    CHECK_FALSE(isPipelineCacheCompatible(tiny, 0x10DE, 0x2786, uuid, &reason));
    CHECK(reason == "file too small");
    auto badSize = blob(0x10DE, 0x2786, 7, 0);
    badSize[0] = 0xFF; // headerSize larger than the file
    CHECK_FALSE(isPipelineCacheCompatible(badSize, 0x10DE, 0x2786, uuid, &reason));
}
