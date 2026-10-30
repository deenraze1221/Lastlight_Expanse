#pragma once

#include "Renderer/Sampler.h"

#include <volk.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace raven::gfx {

class VulkanContext;

// Global descriptor set 0 shared by every engine and game shader:
//   binding 0: texture2D  ravenTextures[kMaxTextures]  (update-after-bind, partially bound)
//   binding 1: sampler    ravenSamplers[kMaxSamplers]  (update-after-bind, partially bound)
//   binding 2: storage buffer of material parameters   (written by MaterialSystem)
// Shaders index these arrays with indices stored in materials or push constants
// (see Shaders/Include/raven/bindless.glsl).
class BindlessRegistry {
public:
    static constexpr uint32_t kMaxTextures = 4096;
    static constexpr uint32_t kMaxSamplers = 64;
    static constexpr uint32_t kMaterialBufferBinding = 2;

    explicit BindlessRegistry(const VulkanContext& context);
    ~BindlessRegistry();

    BindlessRegistry(const BindlessRegistry&) = delete;
    BindlessRegistry& operator=(const BindlessRegistry&) = delete;

    // Registers a sampled image view (layout SHADER_READ_ONLY_OPTIMAL) and returns its slot.
    uint32_t registerTexture(VkImageView view);
    // Rewrites an existing slot (e.g. a render target recreated on resize).
    void updateTexture(uint32_t slot, VkImageView view);
    // Returns a slot to the free list. The caller guarantees no in-flight GPU work uses it.
    void releaseTexture(uint32_t slot);
    uint32_t textureCount() const { return m_nextTexture - static_cast<uint32_t>(m_freeTextures.size()); }

    // Returns the slot of an equivalent sampler, creating it on first use.
    uint32_t sampler(const SamplerDesc& desc);
    uint32_t samplerCount() const { return static_cast<uint32_t>(m_samplers.size()); }

    void setMaterialBuffer(VkBuffer buffer, VkDeviceSize size);

    VkDescriptorSetLayout layout() const { return m_layout; }
    VkDescriptorSet set() const { return m_set; }

private:
    const VulkanContext& m_context;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    uint32_t m_nextTexture = 0;
    std::vector<uint32_t> m_freeTextures;
    std::vector<VkSampler> m_samplers;
    std::unordered_map<SamplerDesc, uint32_t, SamplerDescHash> m_samplerSlots;
};

} // namespace raven::gfx
