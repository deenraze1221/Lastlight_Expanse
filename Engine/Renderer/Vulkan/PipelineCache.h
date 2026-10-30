#pragma once

#include <volk.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace zodar::gfx {

// Validates a serialized VkPipelineCache blob against the current device before it is
// handed to the driver (drivers are not required to reject foreign or corrupted data
// gracefully). Pure function; unit tested.
bool isPipelineCacheCompatible(std::span<const uint8_t> data, uint32_t vendorId, uint32_t deviceId,
                               const std::array<uint8_t, VK_UUID_SIZE>& uuid, std::string* reason = nullptr);

// VkPipelineCache persisted to disk. An empty path keeps the cache in memory only.
// The file is replaced atomically (write to a temporary file, then rename).
class PipelineCache {
public:
    PipelineCache(VkDevice device, uint32_t vendorId, uint32_t deviceId, const std::array<uint8_t, VK_UUID_SIZE>& uuid,
                  std::filesystem::path path);
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    VkPipelineCache handle() const { return m_cache; }
    // Writes the current cache contents. Returns the number of bytes written (0 on failure
    // or when no path is configured).
    size_t save() const;
    size_t loadedBytes() const { return m_loadedBytes; }
    const std::filesystem::path& path() const { return m_path; }

private:
    VkDevice m_device;
    VkPipelineCache m_cache = VK_NULL_HANDLE;
    std::filesystem::path m_path;
    size_t m_loadedBytes = 0;
};

} // namespace zodar::gfx
