#pragma once

#include "Renderer/Sampler.h"
#include "Renderer/Resources/GpuBuffer.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zodar::gfx {

class BindlessRegistry;
class Texture;
class VulkanContext;

using ShaderHandle = uint32_t;
using MaterialHandle = uint32_t;

enum class AlphaMode { Opaque, Mask, Blend };

// The texture pointer is only read by MaterialSystem::createMaterial(), which stores the
// texture's bindless slot. The texture must outlive every material that references it.
struct TextureBinding {
    const Texture* texture = nullptr; // nullptr: neutral default (white, or flat normal).
    SamplerDesc sampler;
};

// Style-neutral material description. The shader decides how parameters are used:
// engine shaders implement PBR metallic-roughness ("zodar/pbr") and unlit ("zodar/unlit");
// games register their own shaders and may use custom0/custom1 freely.
struct MaterialDesc {
    std::string name;
    ShaderHandle shader = 0;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;

    glm::vec4 baseColorFactor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    glm::vec3 emissive{0.0f}; // Linear radiance (glTF emissiveFactor * strength).

    TextureBinding baseColor;
    TextureBinding metallicRoughness; // glTF packing: G = roughness, B = metallic.
    TextureBinding normal;
    TextureBinding occlusion;         // R channel.
    TextureBinding emissiveTexture;

    glm::vec4 custom0{0.0f};
    glm::vec4 custom1{0.0f};
};

// GPU layout of one material (std430). Mirrors ZodarMaterial in zodar/scene.glsl.
struct GpuMaterial {
    glm::vec4 baseColorFactor;
    glm::vec4 emissiveAlphaCutoff;
    float metallic, roughness, normalScale, occlusionStrength;
    uint32_t baseColorTexture, metallicRoughnessTexture, normalTexture, occlusionTexture;
    uint32_t emissiveTexture, baseColorSampler, metallicRoughnessSampler, normalSampler;
    uint32_t occlusionSampler, emissiveSampler, flags, reserved;
    glm::vec4 custom0;
    glm::vec4 custom1;
};
static_assert(sizeof(GpuMaterial) == 128, "GpuMaterial must match zodar/scene.glsl");

enum MaterialFlags : uint32_t {
    kMaterialAlphaMask = 1u,
    kMaterialAlphaBlend = 2u,
    kMaterialDoubleSided = 4u,
};

struct DefaultTextureSlots {
    uint32_t white = 0;
    uint32_t flatNormal = 0;
};

// Owns material shaders, the GPU material table and the pipeline cache. Pipelines are
// keyed by (shader, alpha mode, double-sidedness) and created on first use.
class MaterialSystem {
public:
    static constexpr uint32_t kMaxMaterials = 4096;
    static constexpr const char* kPbrShader = "zodar/pbr";
    static constexpr const char* kUnlitShader = "zodar/unlit";

    struct TargetFormats {
        VkFormat color = VK_FORMAT_UNDEFINED;
        VkFormat depth = VK_FORMAT_UNDEFINED;
    };

    MaterialSystem(const VulkanContext& context, BindlessRegistry& bindless, VkPipelineLayout layout,
                   TargetFormats formats, DefaultTextureSlots defaults, const std::filesystem::path& shaderDirectory);
    ~MaterialSystem();

    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;

    // Registers a vertex/fragment SPIR-V pair under a unique name. Paths are relative to
    // the shader directory unless absolute. Engine shaders use the "zodar/" prefix.
    ShaderHandle registerShader(const std::string& name, const std::filesystem::path& vertexSpv,
                                const std::filesystem::path& fragmentSpv);
    ShaderHandle findShader(const std::string& name) const;
    ShaderHandle pbrShader() const { return m_pbr; }
    ShaderHandle unlitShader() const { return m_unlit; }

    // Materials are immutable once created (updating in-flight data safely is planned).
    // Reuses a released handle when one is free.
    MaterialHandle createMaterial(const MaterialDesc& desc);

    // Returns a material's table slot for reuse by a later createMaterial (asset unloading).
    // Call only once the GPU no longer reads it (e.g. from a DeletionQueue retire). Drops a
    // pending update of the handle. Fails for an unknown or already released handle.
    void releaseMaterial(MaterialHandle handle);
    uint32_t freeMaterialCount() const { return static_cast<uint32_t>(m_free.size()); }

    // Mutable materials (deferred GPU update): changes factors, textures and custom data of an
    // existing material. The CPU description changes immediately; the GPU table entry is
    // written by recordPendingUpdates() in the next recorded frame, ordered after every
    // earlier queue submission's reads (frames in flight keep their old values). The pipeline
    // state (shader, alphaMode, doubleSided) cannot change: create a new material instead.
    // Fails (zodar::Error) for an unknown handle or a pipeline-state change.
    void updateMaterial(MaterialHandle handle, const MaterialDesc& desc);
    bool hasPendingUpdates() const { return !m_pending.empty(); }
    uint32_t pendingUpdateCount() const { return static_cast<uint32_t>(m_pending.size()); }
    // Records barrier -> vkCmdUpdateBuffer per pending material -> barrier, then clears the
    // queue. Called by SceneRenderer at the start of each frame (render() and addToGraph()).
    void recordPendingUpdates(VkCommandBuffer cmd);
    const MaterialDesc& material(MaterialHandle handle) const { return m_materials.at(handle); }
    uint32_t materialCount() const { return static_cast<uint32_t>(m_materials.size()); }

    // Pipeline for drawing `handle` into the scene target formats.
    const GraphicsPipeline& pipeline(MaterialHandle handle);
    // Depth-only pipeline for the depth prepass (material's vertex shader; masked materials use
    // the engine alpha test, opaque ones no fragment shader). Not valid for blended materials.
    const GraphicsPipeline& depthPipeline(MaterialHandle handle);
    uint32_t pipelineCount() const { return static_cast<uint32_t>(m_pipelines.size()); }

    // Converts a description into its GPU representation (exposed for tests).
    GpuMaterial toGpu(const MaterialDesc& desc);

private:
    struct Shader {
        std::string name;
        std::unique_ptr<ShaderModule> vertex;
        std::unique_ptr<ShaderModule> fragment;
    };

    uint32_t textureSlot(const TextureBinding& binding, uint32_t fallback) const;

    const VulkanContext& m_context;
    BindlessRegistry& m_bindless;
    VkPipelineLayout m_layout;
    TargetFormats m_formats;
    DefaultTextureSlots m_defaults;
    std::filesystem::path m_shaderDirectory;

    std::vector<Shader> m_shaders;
    std::vector<MaterialDesc> m_materials;
    std::vector<MaterialHandle> m_pending; // Unique handles awaiting a GPU table write.
    std::vector<MaterialHandle> m_free;    // Released handles (reused LIFO).
    GpuBuffer m_materialBuffer;
    std::unordered_map<uint64_t, std::unique_ptr<GraphicsPipeline>> m_pipelines;
    ShaderHandle m_pbr = 0;
    ShaderHandle m_unlit = 0;
    std::unique_ptr<ShaderModule> m_depthMask;
    std::unordered_map<uint64_t, std::unique_ptr<GraphicsPipeline>> m_depthPipelines;
};

} // namespace zodar::gfx
