#pragma once

#include "Assets/ImageData.h"
#include "Renderer/Resources/GpuImage.h"

#include <cstdint>
#include <string>

namespace zodar::gfx {

class BindlessRegistry;
class UploadBatch;

// Sampled 2D texture: GPU image plus its bindless slot. Releases the slot on destruction;
// the owner must ensure the GPU no longer uses it (textures normally live until shutdown
// or are destroyed after a device idle).
class Texture {
public:
    Texture() = default;
    // Uploads every level present in `image` (call image.generateMips() first for a chain).
    // sRGB images use an _SRGB format so sampling returns linear values.
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

private:
    void release();

    GpuImage m_image;
    BindlessRegistry* m_registry = nullptr;
    uint32_t m_index = UINT32_MAX;
    std::string m_name;
};

} // namespace zodar::gfx
