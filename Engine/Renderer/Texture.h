#pragma once

#include "Assets/ImageData.h"
#include "Renderer/Resources/GpuImage.h"

#include <cstdint>
#include <string>

namespace zodar::gfx {

class BindlessRegistry;
class UploadBatch;

// Vulkan format for an asset image (BC4/BC5 are always UNORM).
VkFormat textureFormat(PixelFormat format, ColorSpace colorSpace);

// Sampled 2D texture: GPU image plus its bindless slot. Releases the slot on destruction;
// the owner must ensure the GPU no longer uses it (textures normally live until shutdown
// or are destroyed after a device idle).
class Texture {
public:
    Texture() = default;
    // Uploads every level present in `image` (compiled KTX2 assets carry baked mips; for raw
    // images call image.generateMips() first). RGBA8 or BCn; sRGB images use an _SRGB format
    // so sampling returns linear values. BCn is decoded on the CPU if the device cannot
    // sample it. Usable once `batch` has been submitted (and, if async, acquired).
    Texture(UploadBatch& batch, BindlessRegistry& registry, const ImageData& image);
    ~Texture();

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    bool isValid() const { return m_image.isValid(); }
    uint32_t bindlessIndex() const { return m_index; }
    const GpuImage& image() const { return m_image; }
    const std::string& name() const { return m_name; }
    VkFormat format() const { return m_format; }

private:
    void release();

    GpuImage m_image;
    BindlessRegistry* m_registry = nullptr;
    uint32_t m_index = UINT32_MAX;
    std::string m_name;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
};

} // namespace zodar::gfx
