#include "Renderer/Texture.h"

#include "Core/Error.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Resources/UploadContext.h"

#include <utility>
#include <vector>

namespace zodar::gfx {

Texture::Texture(UploadBatch& batch, BindlessRegistry& registry, const ImageData& image) : m_name(image.name) {
    ZODAR_ENSURE(!image.levels.empty() && image.width > 0 && image.height > 0,
                 "texture '" + image.name + "' has no pixel data");
    const VkFormat format =
        image.colorSpace == ColorSpace::Srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
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
