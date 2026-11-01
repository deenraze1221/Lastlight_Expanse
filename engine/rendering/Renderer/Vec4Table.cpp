#include "Renderer/Vec4Table.h"

#include "Core/Error.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace zodar::gfx {

Vec4Table::Vec4Table(UploadBatch& batch, BindlessRegistry& registry, std::span<const glm::vec4> values,
                     const std::string& name) {
    ZODAR_ENSURE(!values.empty(), "vec4 table '" + name + "' is empty");
    const VulkanContext& context = batch.context();
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), kFormat, &props);
    ZODAR_ENSURE((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0,
                 "vec4 table '" + name + "': the device cannot sample R32G32B32A32_SFLOAT images");
    VkPhysicalDeviceProperties device{};
    vkGetPhysicalDeviceProperties(context.physicalDevice(), &device);
    const uint32_t limit = std::min(kMaxEntries, device.limits.maxImageDimension2D);
    ZODAR_ENSURE(values.size() <= limit, "vec4 table '" + name + "': " + std::to_string(values.size()) +
                                             " entries exceed the limit of " + std::to_string(limit));
    m_size = static_cast<uint32_t>(values.size());
    m_image = GpuImage(context, {m_size, 1, kFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                 1, name.c_str()});
    // Register before recording the upload: if registration throws (table full), no copy into
    // the image is pending in `batch`, so destroying the image during unwinding is safe.
    m_index = registry.registerTexture(m_image.view());
    m_registry = &registry;
    std::vector<uint8_t> bytes(values.size() * sizeof(glm::vec4));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    const ImageLevelData level{m_size, 1, bytes};
    try {
        batch.uploadImage(m_image, std::span(&level, 1));
    } catch (...) {
        // uploadImage validates and stages before queuing anything, but stay safe if that
        // changes: release the slot and let the batch own the image until its work is done.
        if (m_registry && m_index != UINT32_MAX) m_registry->releaseTexture(m_index);
        m_registry = nullptr;
        m_index = UINT32_MAX;
        batch.keepAlive(std::move(m_image));
        throw;
    }
}

Vec4Table::~Vec4Table() {
    release();
}

Vec4Table::Vec4Table(Vec4Table&& other) noexcept {
    *this = std::move(other);
}

Vec4Table& Vec4Table::operator=(Vec4Table&& other) noexcept {
    if (this != &other) {
        release();
        m_image = std::move(other.m_image);
        m_registry = std::exchange(other.m_registry, nullptr);
        m_index = std::exchange(other.m_index, UINT32_MAX);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

void Vec4Table::release() {
    if (m_registry && m_index != UINT32_MAX) {
        m_registry->releaseTexture(m_index);
    }
    m_registry = nullptr;
    m_index = UINT32_MAX;
    m_image.reset();
}

} // namespace zodar::gfx
