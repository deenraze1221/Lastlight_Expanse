#include "Renderer/TextureArray.h"

#include "Core/Error.h"

#include <algorithm>
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Texture.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <utility>
#include <vector>

namespace zodar::gfx {

TextureArray::TextureArray(UploadBatch& batch, BindlessRegistry& registry, std::span<const ImageData> layers,
                           const std::string& name) {
    ZODAR_ENSURE(!layers.empty(), "texture array '" + name + "' has no layers");
    const bool decode = isBlockCompressed(layers[0].format) && !batch.context().features().bcTextureCompression;
    std::vector<ImageData> decoded;
    if (decode) {
        decoded.reserve(layers.size());
        for (const ImageData& l : layers) {
            decoded.push_back(decompressToRgba8(l));
        }
        layers = decoded;
    }
    const ImageData& first = layers[0];
    for (const ImageData& l : layers) {
        ZODAR_ENSURE(!l.levels.empty(), "texture array '" + name + "': layer '" + l.name + "' has no pixels");
        ZODAR_ENSURE(l.width == first.width && l.height == first.height && l.format == first.format &&
                         l.colorSpace == first.colorSpace && l.levels.size() == first.levels.size(),
                     "texture array '" + name + "': layer '" + l.name + "' differs in size, format or mips");
        // Byte sizes too (uploadImage checks them per layer, i.e. after earlier layers were
        // queued): bad input data fails here, before anything is registered or recorded.
        for (size_t i = 0; i < l.levels.size(); ++i) {
            ZODAR_ENSURE(l.levels[i].size() == imageLevelSize(textureFormat(l.format, l.colorSpace), l.levelWidth(i),
                                                              l.levelHeight(i)),
                         "texture array '" + name + "': layer '" + l.name + "' mip " + std::to_string(i) +
                             " has the wrong byte size");
        }
    }
    m_format = textureFormat(first.format, first.colorSpace);
    VkPhysicalDeviceProperties device{};
    vkGetPhysicalDeviceProperties(batch.context().physicalDevice(), &device);
    ZODAR_ENSURE(std::max<size_t>(layers.size(), 2) <= device.limits.maxImageArrayLayers,
                 "texture array '" + name + "': " + std::to_string(layers.size()) +
                     " layers exceed the device limit maxImageArrayLayers = " +
                     std::to_string(device.limits.maxImageArrayLayers));
    ImageDesc desc{first.width, first.height, m_format,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   static_cast<uint32_t>(first.levels.size()), name.c_str()};
    desc.arrayLayers = static_cast<uint32_t>(layers.size());
    // A single layer still gets a 2D_ARRAY view so shaders can always use the array path.
    if (desc.arrayLayers == 1) {
        desc.arrayLayers = 2;
    }
    m_image = GpuImage(batch.context(), desc);
    // Register before recording uploads: if registration throws (table full), no copy into the
    // image is pending in `batch`, so destroying the image during unwinding is safe.
    m_index = registry.registerTexture(m_image.view());
    m_registry = &registry;
    // One uploadImage per layer. Each call queues commands that reference the VkImage (the copy
    // and the final layout transition, replayed when the batch is submitted). If a later layer
    // fails (e.g. staging allocation), earlier layers' commands are already queued: the image
    // must outlive the batch, so its ownership moves into the batch (keepAlive) instead of
    // being destroyed here. The bindless slot is released at once (no shader uses it yet).
    try {
        for (uint32_t layer = 0; layer < desc.arrayLayers; ++layer) {
            const ImageData& image = layers[std::min<size_t>(layer, layers.size() - 1)];
            std::vector<ImageLevelData> levels;
            for (size_t i = 0; i < image.levels.size(); ++i) {
                levels.push_back({image.levelWidth(i), image.levelHeight(i), image.levels[i]});
            }
            batch.uploadImage(m_image, levels, layer);
        }
    } catch (...) {
        abandon(batch);
        throw;
    }
}

void TextureArray::abandon(UploadBatch& batch) noexcept {
    if (m_registry && m_index != UINT32_MAX) {
        m_registry->releaseTexture(m_index);
    }
    m_registry = nullptr;
    m_index = UINT32_MAX;
    batch.keepAlive(std::move(m_image));
}

TextureArray::~TextureArray() {
    release();
}

TextureArray::TextureArray(TextureArray&& other) noexcept {
    *this = std::move(other);
}

TextureArray& TextureArray::operator=(TextureArray&& other) noexcept {
    if (this != &other) {
        release();
        m_image = std::move(other.m_image);
        m_registry = std::exchange(other.m_registry, nullptr);
        m_index = std::exchange(other.m_index, UINT32_MAX);
        m_format = std::exchange(other.m_format, VK_FORMAT_UNDEFINED);
    }
    return *this;
}

void TextureArray::release() {
    if (m_registry && m_index != UINT32_MAX) {
        m_registry->releaseTexture(m_index);
    }
    m_registry = nullptr;
    m_index = UINT32_MAX;
    m_image.reset();
}

} // namespace zodar::gfx
