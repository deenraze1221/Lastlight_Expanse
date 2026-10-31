#include "Renderer/BindlessRegistry.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <algorithm>
#include <bit>
#include <string>

namespace zodar::gfx {
namespace {

VkFilter toVk(Filter f) {
    return f == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode toVk(AddressMode m) {
    switch (m) {
    case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

void hashCombine(size_t& seed, size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

} // namespace

size_t SamplerDescHash::operator()(const SamplerDesc& d) const noexcept {
    size_t h = 0;
    hashCombine(h, static_cast<size_t>(d.magFilter));
    hashCombine(h, static_cast<size_t>(d.minFilter));
    hashCombine(h, static_cast<size_t>(d.mipFilter));
    hashCombine(h, static_cast<size_t>(d.addressU));
    hashCombine(h, static_cast<size_t>(d.addressV));
    hashCombine(h, std::bit_cast<uint32_t>(d.maxAnisotropy));
    hashCombine(h, std::bit_cast<uint32_t>(d.mipLodBias));
    hashCombine(h, d.enableMips ? 1u : 0u);
    return h;
}

BindlessRegistry::BindlessRegistry(const VulkanContext& context) : m_context(context) {
    const VkDevice device = context.device();
    const VkShaderStageFlags stages = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures, stages, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLER, kMaxSamplers, stages, nullptr};
    bindings[2] = {kMaterialBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr};

    const VkDescriptorBindingFlags arrayFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    // The material buffer is bound once at startup, before any command buffer uses the set.
    const VkDescriptorBindingFlags flags[3] = {arrayFlags, arrayFlags, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT};
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flagsInfo.bindingCount = 3;
    flagsInfo.pBindingFlags = flags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    ZODAR_VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_layout));

    const VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures},
                                           {VK_DESCRIPTOR_TYPE_SAMPLER, kMaxSamplers},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = sizes;
    VkResult result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool);
    if (result == VK_SUCCESS) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_layout;
        result = vkAllocateDescriptorSets(device, &allocInfo, &m_set);
    }
    if (result != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        vkDestroyDescriptorSetLayout(device, m_layout, nullptr);
        throwVkError(result, "BindlessRegistry setup");
    }
    context.setDebugName(m_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Bindless");
}

BindlessRegistry::~BindlessRegistry() {
    const VkDevice device = m_context.device();
    for (VkSampler sampler : m_samplers) {
        vkDestroySampler(device, sampler, nullptr);
    }
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_layout, nullptr);
}

uint32_t BindlessRegistry::registerTexture(VkImageView view) {
    uint32_t slot = 0;
    if (!m_freeTextures.empty()) {
        slot = m_freeTextures.back();
        m_freeTextures.pop_back();
    } else {
        if (m_nextTexture >= kMaxTextures) {
            fail("Bindless texture table is full (" + std::to_string(kMaxTextures) + " slots)");
        }
        slot = m_nextTexture++;
    }
    updateTexture(slot, view);
    return slot;
}

void BindlessRegistry::updateTexture(uint32_t slot, VkImageView view) {
    ZODAR_ENSURE(slot < m_nextTexture, "updateTexture: slot was never registered");
    VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_set;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(m_context.device(), 1, &write, 0, nullptr);
}

void BindlessRegistry::releaseTexture(uint32_t slot) {
    ZODAR_ENSURE(slot < m_nextTexture, "releaseTexture: invalid slot");
    ZODAR_ENSURE(std::find(m_freeTextures.begin(), m_freeTextures.end(), slot) == m_freeTextures.end(),
                 "releaseTexture: slot released twice");
    m_freeTextures.push_back(slot);
}

uint32_t BindlessRegistry::sampler(const SamplerDesc& desc) {
    if (auto it = m_samplerSlots.find(desc); it != m_samplerSlots.end()) {
        return it->second;
    }
    if (m_samplers.size() >= kMaxSamplers) {
        fail("Bindless sampler table is full");
    }
    const auto& info = m_context.deviceInfo();
    const bool anisotropy = info.samplerAnisotropy && desc.maxAnisotropy > 1.0f;

    VkSamplerCreateInfo create{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    create.magFilter = toVk(desc.magFilter);
    create.minFilter = toVk(desc.minFilter);
    create.mipmapMode = desc.mipFilter == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    create.addressModeU = toVk(desc.addressU);
    create.addressModeV = toVk(desc.addressV);
    create.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create.mipLodBias = desc.mipLodBias;
    create.anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE;
    create.maxAnisotropy = anisotropy ? std::min(desc.maxAnisotropy, info.maxSamplerAnisotropy) : 1.0f;
    create.minLod = 0.0f;
    create.maxLod = desc.enableMips ? VK_LOD_CLAMP_NONE : 0.0f;
    create.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    VkSampler handle = VK_NULL_HANDLE;
    ZODAR_VK_CHECK(vkCreateSampler(m_context.device(), &create, nullptr, &handle));
    const auto slot = static_cast<uint32_t>(m_samplers.size());
    m_samplers.push_back(handle);
    m_samplerSlots.emplace(desc, slot);

    VkDescriptorImageInfo image{handle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_set;
    write.dstBinding = 1;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(m_context.device(), 1, &write, 0, nullptr);
    return slot;
}

void BindlessRegistry::setMaterialBuffer(VkBuffer buffer, VkDeviceSize size) {
    VkDescriptorBufferInfo info{buffer, 0, size};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_set;
    write.dstBinding = kMaterialBufferBinding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(m_context.device(), 1, &write, 0, nullptr);
}

} // namespace zodar::gfx
