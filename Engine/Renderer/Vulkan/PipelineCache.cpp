#include "Renderer/Vulkan/PipelineCache.h"

#include "Core/Log.h"
#include "Renderer/Vulkan/VulkanCheck.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace raven::gfx {

bool isPipelineCacheCompatible(std::span<const uint8_t> data, uint32_t vendorId, uint32_t deviceId,
                               const std::array<uint8_t, VK_UUID_SIZE>& uuid, std::string* reason) {
    const auto reject = [&](const char* why) {
        if (reason) *reason = why;
        return false;
    };
    if (data.size() < sizeof(VkPipelineCacheHeaderVersionOne)) {
        return reject("file too small");
    }
    VkPipelineCacheHeaderVersionOne header;
    std::memcpy(&header, data.data(), sizeof(header));
    if (header.headerSize < sizeof(header) || header.headerSize > data.size()) return reject("bad header size");
    if (header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) return reject("unknown header version");
    if (header.vendorID != vendorId) return reject("different GPU vendor");
    if (header.deviceID != deviceId) return reject("different GPU");
    if (std::memcmp(header.pipelineCacheUUID, uuid.data(), VK_UUID_SIZE) != 0) {
        return reject("different driver version");
    }
    return true;
}

PipelineCache::PipelineCache(VkDevice device, uint32_t vendorId, uint32_t deviceId,
                             const std::array<uint8_t, VK_UUID_SIZE>& uuid, std::filesystem::path path)
    : m_device(device), m_path(std::move(path)) {
    std::vector<uint8_t> data;
    if (!m_path.empty()) {
        std::ifstream file(m_path, std::ios::binary | std::ios::ate);
        if (file) {
            data.resize(static_cast<size_t>(file.tellg()));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            std::string reason;
            if (!file || !isPipelineCacheCompatible(data, vendorId, deviceId, uuid, &reason)) {
                RAVEN_LOG_WARN("Ignoring pipeline cache '{}': {}", m_path.string(), file ? reason : "read error");
                data.clear();
            }
        }
    }
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    RAVEN_VK_CHECK(vkCreatePipelineCache(device, &info, nullptr, &m_cache));
    m_loadedBytes = data.size();
    if (!m_path.empty()) {
        RAVEN_LOG_INFO("Pipeline cache: {} ({} KiB loaded)", m_path.string(), m_loadedBytes / 1024);
    }
}

PipelineCache::~PipelineCache() {
    save();
    vkDestroyPipelineCache(m_device, m_cache, nullptr);
}

size_t PipelineCache::save() const {
    if (m_path.empty()) {
        return 0;
    }
    size_t size = 0;
    if (vkGetPipelineCacheData(m_device, m_cache, &size, nullptr) != VK_SUCCESS || size == 0) {
        return 0;
    }
    std::vector<uint8_t> data(size);
    if (vkGetPipelineCacheData(m_device, m_cache, &size, data.data()) != VK_SUCCESS) {
        return 0;
    }
    std::error_code ec;
    std::filesystem::create_directories(m_path.parent_path(), ec);
    const std::filesystem::path temp = m_path.string() + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(size));
        if (!file) {
            RAVEN_LOG_WARN("Cannot write pipeline cache '{}'", temp.string());
            return 0;
        }
    }
    std::filesystem::rename(temp, m_path, ec);
    if (ec) {
        RAVEN_LOG_WARN("Cannot replace pipeline cache '{}': {}", m_path.string(), ec.message());
        return 0;
    }
    return size;
}

} // namespace raven::gfx
