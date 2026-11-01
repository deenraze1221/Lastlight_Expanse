#pragma once

#include "Renderer/Resources/GpuImage.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace zodar::gfx {

class BindlessRegistry;
class UploadBatch;

// Small read-only table of vec4 values for shaders: an N x 1 VK_FORMAT_R32G32B32A32_SFLOAT
// image registered in the bindless texture table. Shaders read entry i exactly with
// zodarFetch(index, sampler, ivec2(i, 0)) (texelFetch: no filtering, no conversion), so
// values round-trip bit-exactly (full float32 precision; unlike RGBA8/RGBA16F). Used by
// zodar/voxel for per-layer material parameters. The format must support sampled images
// (mandatory in Vulkan; checked, zodar::Error otherwise).
class Vec4Table {
public:
    static constexpr VkFormat kFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    // Upper bound on entries; the device's maxImageDimension2D (Vulkan minimum 4096) also applies.
    static constexpr uint32_t kMaxEntries = 16384;

    Vec4Table() = default;
    Vec4Table(UploadBatch& batch, BindlessRegistry& registry, std::span<const glm::vec4> values,
              const std::string& name = "Vec4Table");
    ~Vec4Table();
    Vec4Table(Vec4Table&& other) noexcept;
    Vec4Table& operator=(Vec4Table&& other) noexcept;
    Vec4Table(const Vec4Table&) = delete;
    Vec4Table& operator=(const Vec4Table&) = delete;

    bool isValid() const { return m_image.isValid(); }
    uint32_t bindlessIndex() const { return m_index; }
    uint32_t size() const { return m_size; }
    const GpuImage& image() const { return m_image; }

private:
    void release();

    GpuImage m_image;
    BindlessRegistry* m_registry = nullptr;
    uint32_t m_index = UINT32_MAX;
    uint32_t m_size = 0;
};

} // namespace zodar::gfx
