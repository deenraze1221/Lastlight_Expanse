#include "Renderer/Lighting/Environment.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

#include <array>
#include <chrono>
#include <vector>

namespace zodar::gfx {
namespace {

constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkImageUsageFlags kBakedUsage =
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

struct SizePush {
    uint32_t size;
    uint32_t samples;
    uint32_t environmentSize;
    uint32_t environmentMips;
    float roughness;
};

void computeBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void toGeneral(VkCommandBuffer cmd, const GpuImage& image) {
    transitionImage(cmd, image.handle(), image.fullRange(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void toShaderRead(VkCommandBuffer cmd, const GpuImage& image) {
    transitionImage(cmd, image.handle(), image.fullRange(), VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

uint32_t mipSize(uint32_t size, uint32_t mip) {
    return std::max(1u, size >> mip);
}

} // namespace

IblSettings iblSettings(LightingQuality quality) {
    switch (quality) {
    case LightingQuality::Low: return {128, 16, 64, 5, 128, 128};
    case LightingQuality::Medium: return {256, 32, 128, 6, 256, 256};
    case LightingQuality::High: return {512, 32, 256, 6, 512, 512};
    case LightingQuality::Ultra: return {1024, 32, 512, 7, 1024, 1024};
    }
    return {};
}

Environment::~Environment() {
    // Frames in flight may still sample the cubes: release slots and memory later.
    auto images = std::make_shared<std::array<GpuImage, 3>>();
    (*images)[0] = std::move(m_radiance);
    (*images)[1] = std::move(m_irradiance);
    (*images)[2] = std::move(m_prefiltered);
    BindlessRegistry* bindless = &m_bindless;
    const uint32_t slots[3] = {m_radianceSlot, m_irradianceSlot, m_prefilteredSlot};
    m_context.deletionQueue().retire([images, bindless, a = slots[0], b = slots[1], c = slots[2]]() mutable {
        for (uint32_t slot : {a, b, c}) {
            if (slot != UINT32_MAX) bindless->releaseTexture(slot);
        }
        images.reset();
    });
}

VkDeviceSize Environment::gpuBytes() const {
    const auto bytes = [](const GpuImage& image) {
        VkDeviceSize total = 0;
        for (uint32_t m = 0; m < image.mipLevels(); ++m) {
            total += VkDeviceSize(mipSize(image.extent().width, m)) * mipSize(image.extent().height, m) * 8 *
                     image.layers();
        }
        return total;
    };
    return bytes(m_radiance) + bytes(m_irradiance) + bytes(m_prefiltered);
}

IblBaker::IblBaker(VulkanContext& context, UploadContext& upload, BindlessRegistry& bindless,
                   const std::filesystem::path& shaderDirectory)
    : m_context(context), m_upload(upload), m_bindless(bindless) {
    const VkDevice device = context.device();
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    ZODAR_VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_setLayout));
    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    ZODAR_VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool));

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    ZODAR_VK_CHECK(vkCreateSampler(device, &sampler, nullptr, &m_linearClamp));
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // Longitude wraps
    sampler.maxLod = 0.0f;
    ZODAR_VK_CHECK(vkCreateSampler(device, &sampler, nullptr, &m_equirectSampler));

    const auto pipeline = [&](const char* file, const char* name) {
        return std::make_unique<ComputePipeline>(context, loadSpirv(shaderDirectory / file),
                                                 std::vector<VkDescriptorSetLayout>{m_setLayout}, sizeof(SizePush), name);
    };
    m_equirect = pipeline("ibl_equirect.comp.spv", "IblEquirect");
    m_downsample = pipeline("ibl_downsample.comp.spv", "IblDownsample");
    m_irradiance = pipeline("ibl_irradiance.comp.spv", "IblIrradiance");
    m_prefilter = pipeline("ibl_prefilter.comp.spv", "IblPrefilter");
    m_dfgPipeline = pipeline("ibl_dfg.comp.spv", "IblDfg");

    SamplerDesc clamp = SamplerDesc::linear();
    clamp.addressU = clamp.addressV = AddressMode::ClampToEdge;
    clamp.maxAnisotropy = 1.0f;
    m_samplerSlot = bindless.sampler(clamp);

    // DFG LUT, once.
    m_dfg = GpuImage(context, {kDfgLutSize, kDfgLutSize, kHdrFormat, kBakedUsage, 1, "IblDfgLut"});
    const VkDescriptorSet set = allocateSet(VK_NULL_HANDLE, VK_NULL_HANDLE, m_dfg.view());
    upload.immediate([&](VkCommandBuffer cmd) {
        toGeneral(cmd, m_dfg);
        const SizePush push{kDfgLutSize, 1024, 0, 0, 0.0f};
        dispatch(cmd, *m_dfgPipeline, set, &push, sizeof(push), kDfgLutSize, kDfgLutSize, 1);
        toShaderRead(cmd, m_dfg);
    });
    ZODAR_VK_CHECK(vkResetDescriptorPool(device, m_pool, 0));
    m_dfgSlot = bindless.registerTexture(m_dfg.view());
}

IblBaker::~IblBaker() {
    const VkDevice device = m_context.device();
    vkDeviceWaitIdle(device);
    if (m_dfgSlot != UINT32_MAX) m_bindless.releaseTexture(m_dfgSlot);
    vkDestroySampler(device, m_linearClamp, nullptr);
    vkDestroySampler(device, m_equirectSampler, nullptr);
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
}

VkDescriptorSet IblBaker::allocateSet(VkImageView sampled, VkSampler sampler, VkImageView storage,
                                      VkImageLayout sampledLayout) {
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = m_pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &m_setLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    ZODAR_VK_CHECK(vkAllocateDescriptorSets(m_context.device(), &info, &set));
    VkDescriptorImageInfo images[2] = {{sampler, sampled, sampledLayout},
                                       {VK_NULL_HANDLE, storage, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[2]{};
    uint32_t count = 0;
    if (sampled) {
        writes[count] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[count].dstSet = set;
        writes[count].dstBinding = 0;
        writes[count].descriptorCount = 1;
        writes[count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[count].pImageInfo = &images[0];
        ++count;
    }
    writes[count] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[count].dstSet = set;
    writes[count].dstBinding = 1;
    writes[count].descriptorCount = 1;
    writes[count].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[count].pImageInfo = &images[1];
    ++count;
    vkUpdateDescriptorSets(m_context.device(), count, writes, 0, nullptr);
    return set;
}

void IblBaker::dispatch(VkCommandBuffer cmd, const ComputePipeline& pipeline, VkDescriptorSet set, const void* push,
                        uint32_t pushSize, uint32_t width, uint32_t height, uint32_t layers) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize, push);
    vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, layers);
}

std::unique_ptr<Environment> IblBaker::bake(const ImageData& equirect, const IblSettings& s) {
    ZODAR_ENSURE(equirect.format == PixelFormat::Rgba16f && !equirect.levels.empty(),
                 "IblBaker::bake: '" + equirect.name + "' must be a linear Rgba16f equirectangular image");
    ZODAR_ENSURE(s.environmentSize >= 4 && s.irradianceSize >= 1 && s.prefilteredSize >= 4 && s.prefilteredMips >= 1,
                 "IblBaker::bake: invalid settings");
    const auto start = std::chrono::steady_clock::now();
    const VkDevice device = m_context.device();

    GpuImage source(m_context, {equirect.width, equirect.height, kHdrFormat,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "IblEquirectSource"});
    {
        UploadBatch batch(m_upload);
        const ImageLevelData level{equirect.width, equirect.height, equirect.levels[0]};
        batch.uploadImage(source, std::span(&level, 1));
        batch.submit();
    }

    std::unique_ptr<Environment> env(new Environment(m_context, m_bindless));
    const uint32_t envMips = fullMipCount(s.environmentSize, s.environmentSize);
    const uint32_t prefMips = std::min(s.prefilteredMips, fullMipCount(s.prefilteredSize, s.prefilteredSize));
    env->m_radiance = GpuImage(m_context, {s.environmentSize, s.environmentSize, kHdrFormat, kBakedUsage, envMips,
                                           "IblRadiance", true});
    env->m_irradiance = GpuImage(m_context, {s.irradianceSize, s.irradianceSize, kHdrFormat, kBakedUsage, 1,
                                             "IblIrradiance", true});
    env->m_prefiltered = GpuImage(m_context, {s.prefilteredSize, s.prefilteredSize, kHdrFormat, kBakedUsage, prefMips,
                                              "IblPrefiltered", true});

    // Views for the bake (destroyed after the blocking submission).
    std::vector<VkImageView> views;
    const auto view = [&](const GpuImage& image, VkImageViewType type, uint32_t mip, uint32_t mips) {
        views.push_back(createImageView(device, image.handle(), kHdrFormat, type,
                                        {VK_IMAGE_ASPECT_COLOR_BIT, mip, mips, 0, 6}));
        return views.back();
    };
    const VkImageView radianceAll = env->m_radiance.view();

    m_upload.immediate([&](VkCommandBuffer cmd) {
        toGeneral(cmd, env->m_radiance);
        toGeneral(cmd, env->m_irradiance);
        toGeneral(cmd, env->m_prefiltered);
        // 1. Equirectangular -> radiance cube mip 0.
        SizePush push{s.environmentSize, 0, 0, 0, 0.0f};
        dispatch(cmd, *m_equirect,
                 allocateSet(source.view(), m_equirectSampler, view(env->m_radiance, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                 &push, sizeof(push), s.environmentSize, s.environmentSize, 6);
        // 2. Radiance mip chain.
        for (uint32_t m = 1; m < envMips; ++m) {
            computeBarrier(cmd);
            push.size = mipSize(s.environmentSize, m);
            dispatch(cmd, *m_downsample,
                     allocateSet(view(env->m_radiance, VK_IMAGE_VIEW_TYPE_CUBE, m - 1, 1), m_linearClamp,
                                 view(env->m_radiance, VK_IMAGE_VIEW_TYPE_2D_ARRAY, m, 1)),
                     &push, sizeof(push), push.size, push.size, 6);
        }
        computeBarrier(cmd);
        // 3. Diffuse irradiance.
        push = {s.irradianceSize, s.irradianceSamples, s.environmentSize, envMips, 0.0f};
        dispatch(cmd, *m_irradiance,
                 allocateSet(radianceAll, m_linearClamp, view(env->m_irradiance, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1)),
                 &push, sizeof(push), s.irradianceSize, s.irradianceSize, 6);
        // 4. Specular prefilter, roughness = mip / (mips - 1).
        for (uint32_t m = 0; m < prefMips; ++m) {
            push = {mipSize(s.prefilteredSize, m), s.prefilterSamples, s.environmentSize, envMips,
                    prefMips > 1 ? float(m) / float(prefMips - 1) : 0.0f};
            dispatch(cmd, *m_prefilter,
                     allocateSet(radianceAll, m_linearClamp, view(env->m_prefiltered, VK_IMAGE_VIEW_TYPE_2D_ARRAY, m, 1)),
                     &push, sizeof(push), push.size, push.size, 6);
        }
        toShaderRead(cmd, env->m_radiance);
        toShaderRead(cmd, env->m_irradiance);
        toShaderRead(cmd, env->m_prefiltered);
    });
    for (VkImageView v : views) vkDestroyImageView(device, v, nullptr);
    ZODAR_VK_CHECK(vkResetDescriptorPool(device, m_pool, 0));

    env->m_radianceSlot = m_bindless.registerTexture(env->m_radiance.view());
    env->m_irradianceSlot = m_bindless.registerTexture(env->m_irradiance.view());
    env->m_prefilteredSlot = m_bindless.registerTexture(env->m_prefiltered.view());
    env->m_bakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ZODAR_LOG_INFO("IBL '{}': radiance {}^2 x{} mips, irradiance {}^2, prefiltered {}^2 x{} mips, {:.1f} MiB, {:.1f} ms",
                   equirect.name, s.environmentSize, envMips, s.irradianceSize, s.prefilteredSize, prefMips,
                   double(env->gpuBytes()) / 1048576.0, env->m_bakeMs);
    return env;
}

} // namespace zodar::gfx
