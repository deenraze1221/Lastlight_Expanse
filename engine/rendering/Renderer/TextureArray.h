#pragma once

#include "Assets/ImageData.h"
#include "Renderer/Resources/GpuImage.h"

#include <cstdint>
#include <span>
#include <string>

namespace zodar::gfx {

class BindlessRegistry;
class UploadBatch;

// Sampled 2D texture array: one image with N layers of identical size, format and mip
// count, registered in the bindless table as a 2D_ARRAY view. Shaders sample it with
// zodarSampleArray(index, sampler, vec3(uv, layer)) (zodar/bindless.glsl). Typical use: block
// / tile atlases without atlas bleeding, one descriptor for hundreds of materials.
// Each layer is uploaded with all of its levels; layers may be BCn if the device samples BC
// (otherwise decoded on the CPU like Texture).
class TextureArray {
public:
    TextureArray() = default;
    TextureArray(UploadBatch& batch, BindlessRegistry& registry, std::span<const ImageData> layers,
                 const std::string& name = "TextureArray");
    ~TextureArray();

    TextureArray(TextureArray&& other) noexcept;
    TextureArray& operator=(TextureArray&& other) noexcept;
    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;

    bool isValid() const { return m_image.isValid(); }
    uint32_t bindlessIndex() const { return m_index; }
    uint32_t layerCount() const { return m_image.layers(); }
    const GpuImage& image() const { return m_image; }
    VkFormat format() const { return m_format; }

private:
    void release();

    GpuImage m_image;
    BindlessRegistry* m_registry = nullptr;
    uint32_t m_index = UINT32_MAX;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
};

} // namespace zodar::gfx
