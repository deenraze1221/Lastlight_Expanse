#include "Renderer/Texture.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <utility>
#include <vector>

namespace zodar::gfx {

VkFormat textureFormat(PixelFormat format, ColorSpace colorSpace) {
    const bool srgb = colorSpace == ColorSpace::Srgb;
    switch (format) {
    case PixelFormat::Rgba8: return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::Bc1: return srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case PixelFormat::Bc3: return srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
    case PixelFormat::Bc4: return VK_FORMAT_BC4_UNORM_BLOCK;
    case PixelFormat::Bc5: return VK_FORMAT_BC5_UNORM_BLOCK;
    case PixelFormat::Bc7: return srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
    case PixelFormat::Rgba16f: return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

Texture::Texture(UploadBatch& batch, BindlessRegistry& registry, const ImageData& source) : m_name(source.name) {
    ZODAR_ENSURE(!source.levels.empty() && source.width > 0 && source.height > 0,
                 "texture '" + source.name + "' has no pixel data");
    // Without BCn sampling support compressed assets are decoded on the CPU (fallback).
    ImageData decoded;
    const ImageData* data = &source;
    if (isBlockCompressed(source.format) && !batch.context().features().bcTextureCompression) {
        ZODAR_LOG_WARN("Texture '{}': device cannot sample {}; decoding to RGBA8 (fallback)", source.name,
                       toString(source.format));
        decoded = decompressToRgba8(source);
        data = &decoded;
    }
    const ImageData& image = *data;
    const VkFormat format = textureFormat(image.format, image.colorSpace);
    m_format = format;
    const auto mips = static_cast<uint32_t>(image.levels.size());
    m_image = GpuImage(batch.context(), {image.width, image.height, format,
                                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                          mips, image.name.empty() ? "Texture" : image.name.c_str()});

    std::vector<ImageLevelData> levels;
    for (size_t i = 0; i < image.levels.size(); ++i) {
        levels.push_back({image.levelWidth(i), image.levelHeight(i), image.levels[i]});
    }
    batch.uploadImage(m_image, levels);

    m_registry = &registry;
    m_index = registry.registerTexture(m_image.view());
}

Texture::~Texture() {
    release();
}

Texture::Texture(Texture&& other) noexcept {
    *this = std::move(other);
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        release();
        m_image = std::move(other.m_image);
        m_registry = std::exchange(other.m_registry, nullptr);
        m_index = std::exchange(other.m_index, UINT32_MAX);
        m_name = std::move(other.m_name);
        m_format = std::exchange(other.m_format, VK_FORMAT_UNDEFINED);
    }
    return *this;
}

void Texture::release() {
    if (m_registry && m_index != UINT32_MAX) {
        m_registry->releaseTexture(m_index);
    }
    m_registry = nullptr;
    m_index = UINT32_MAX;
    m_image.reset();
}

} // namespace zodar::gfx
