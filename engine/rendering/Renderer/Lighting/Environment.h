#pragma once

#include "Assets/ImageData.h"
#include "Renderer/Lighting/ClusterMath.h"
#include "Renderer/Resources/GpuImage.h"

#include <volk.h>

#include <filesystem>
#include <memory>

namespace zodar::gfx {

class BindlessRegistry;
class ComputePipeline;
class UploadContext;
class VulkanContext;

struct IblSettings {
    uint32_t environmentSize = 512;  // Cube face size of the radiance cube (full mip chain)
    uint32_t irradianceSize = 32;
    uint32_t prefilteredSize = 256;
    uint32_t prefilteredMips = 6;    // Roughness 0, 0.2, ..., 1
    uint32_t irradianceSamples = 512;
    uint32_t prefilterSamples = 512;
};
IblSettings iblSettings(LightingQuality quality);

// Baked image-based lighting for one environment: radiance cube (skybox and prefilter
// source), diffuse irradiance cube and GGX-prefiltered specular cube, all RGBA16F and
// registered in the bindless table (cube views). Created by IblBaker; immutable.
// Destruction is deferred through the deletion queue (frames in flight may sample it).
class Environment {
public:
    ~Environment();
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;

    uint32_t radianceSlot() const { return m_radianceSlot; }
    uint32_t irradianceSlot() const { return m_irradianceSlot; }
    uint32_t prefilteredSlot() const { return m_prefilteredSlot; }
    uint32_t prefilteredMips() const { return m_prefiltered.mipLevels(); }
    uint32_t radianceMips() const { return m_radiance.mipLevels(); }
    const GpuImage& radiance() const { return m_radiance; }
    const GpuImage& irradiance() const { return m_irradiance; }
    const GpuImage& prefiltered() const { return m_prefiltered; }
    VkDeviceSize gpuBytes() const;
    double bakeMs() const { return m_bakeMs; }

private:
    friend class IblBaker;
    Environment(VulkanContext& context, BindlessRegistry& bindless) : m_context(context), m_bindless(bindless) {}
    VulkanContext& m_context;
    BindlessRegistry& m_bindless;
    GpuImage m_radiance, m_irradiance, m_prefiltered;
    uint32_t m_radianceSlot = UINT32_MAX, m_irradianceSlot = UINT32_MAX, m_prefilteredSlot = UINT32_MAX;
    double m_bakeMs = 0.0;
};

// GPU baking of image-based lighting with compute shaders (ibl_*.comp). The DFG LUT for the
// split sum and multiple-scattering compensation is baked once at construction and is used
// by every frame (also without an environment). Environment bakes record into a blocking
// graphics-queue submission (load time); the source image is uploaded on the transfer queue.
class IblBaker {
public:
    static constexpr uint32_t kDfgLutSize = 128;

    IblBaker(VulkanContext& context, UploadContext& upload, BindlessRegistry& bindless,
             const std::filesystem::path& shaderDirectory);
    ~IblBaker();

    IblBaker(const IblBaker&) = delete;
    IblBaker& operator=(const IblBaker&) = delete;

    // `equirect`: linear Rgba16f equirectangular image (loadHdrImage, proceduralSky, ...).
    std::unique_ptr<Environment> bake(const ImageData& equirect, const IblSettings& settings = {});

    uint32_t dfgLutSlot() const { return m_dfgSlot; }
    const GpuImage& dfgLut() const { return m_dfg; }
    // Bindless sampler: linear, clamp-to-edge, mipmapped (for cubes and the LUT).
    uint32_t samplerSlot() const { return m_samplerSlot; }

private:
    VkDescriptorSet allocateSet(VkImageView sampled, VkSampler sampler, VkImageView storage,
                                VkImageLayout sampledLayout = VK_IMAGE_LAYOUT_GENERAL);
    void dispatch(VkCommandBuffer cmd, const ComputePipeline& pipeline, VkDescriptorSet set, const void* push,
                  uint32_t pushSize, uint32_t width, uint32_t height, uint32_t layers) const;

    VulkanContext& m_context;
    UploadContext& m_upload;
    BindlessRegistry& m_bindless;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkSampler m_linearClamp = VK_NULL_HANDLE;
    VkSampler m_equirectSampler = VK_NULL_HANDLE;
    std::unique_ptr<ComputePipeline> m_equirect, m_downsample, m_irradiance, m_prefilter, m_dfgPipeline;
    GpuImage m_dfg;
    uint32_t m_dfgSlot = UINT32_MAX;
    uint32_t m_samplerSlot = 0;
};

} // namespace zodar::gfx
