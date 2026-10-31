#include "Renderer/Material.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Texture.h"
#include "Renderer/Vulkan/VulkanContext.h"

namespace zodar::gfx {

MaterialSystem::MaterialSystem(const VulkanContext& context, BindlessRegistry& bindless, VkPipelineLayout layout,
                               TargetFormats formats, DefaultTextureSlots defaults,
                               const std::filesystem::path& shaderDirectory)
    : m_context(context), m_bindless(bindless), m_layout(layout), m_formats(formats), m_defaults(defaults),
      m_shaderDirectory(shaderDirectory) {
    // Host-visible table: a new material's slot is never read by in-flight frames.
    m_materialBuffer = GpuBuffer(context, {sizeof(GpuMaterial) * kMaxMaterials, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           MemoryUsage::Upload, "MaterialTable"});
    m_bindless.setMaterialBuffer(m_materialBuffer.handle(), m_materialBuffer.size());

    m_pbr = registerShader(kPbrShader, "mesh.vert.spv", "pbr.frag.spv");
    m_unlit = registerShader(kUnlitShader, "mesh.vert.spv", "unlit.frag.spv");
    m_depthMask = std::make_unique<ShaderModule>(context.device(), loadSpirv(shaderDirectory / "depth_mask.frag.spv"));
}

MaterialSystem::~MaterialSystem() = default;

ShaderHandle MaterialSystem::registerShader(const std::string& name, const std::filesystem::path& vertexSpv,
                                            const std::filesystem::path& fragmentSpv) {
    for (const auto& shader : m_shaders) {
        if (shader.name == name) {
            fail("Shader '" + name + "' is already registered");
        }
    }
    const auto resolve = [&](const std::filesystem::path& p) { return p.is_absolute() ? p : m_shaderDirectory / p; };
    Shader shader;
    shader.name = name;
    shader.vertex = std::make_unique<ShaderModule>(m_context.device(), loadSpirv(resolve(vertexSpv)));
    shader.fragment = std::make_unique<ShaderModule>(m_context.device(), loadSpirv(resolve(fragmentSpv)));
    m_shaders.push_back(std::move(shader));
    return static_cast<ShaderHandle>(m_shaders.size() - 1);
}

ShaderHandle MaterialSystem::findShader(const std::string& name) const {
    for (size_t i = 0; i < m_shaders.size(); ++i) {
        if (m_shaders[i].name == name) {
            return static_cast<ShaderHandle>(i);
        }
    }
    fail("Unknown shader '" + name + "'");
}

uint32_t MaterialSystem::textureSlot(const TextureBinding& binding, uint32_t fallback) const {
    if (!binding.texture) {
        return fallback;
    }
    ZODAR_ENSURE(binding.texture->isValid(), "material references an empty texture");
    return binding.texture->bindlessIndex();
}

GpuMaterial MaterialSystem::toGpu(const MaterialDesc& d) {
    GpuMaterial g{};
    g.baseColorFactor = d.baseColorFactor;
    g.emissiveAlphaCutoff = glm::vec4(d.emissive, d.alphaCutoff);
    g.metallic = d.metallic;
    g.roughness = d.roughness;
    g.normalScale = d.normalScale;
    g.occlusionStrength = d.occlusionStrength;
    g.baseColorTexture = textureSlot(d.baseColor, m_defaults.white);
    g.metallicRoughnessTexture = textureSlot(d.metallicRoughness, m_defaults.white);
    g.normalTexture = textureSlot(d.normal, m_defaults.flatNormal);
    g.occlusionTexture = textureSlot(d.occlusion, m_defaults.white);
    g.emissiveTexture = textureSlot(d.emissiveTexture, m_defaults.white);
    g.baseColorSampler = m_bindless.sampler(d.baseColor.sampler);
    g.metallicRoughnessSampler = m_bindless.sampler(d.metallicRoughness.sampler);
    g.normalSampler = m_bindless.sampler(d.normal.sampler);
    g.occlusionSampler = m_bindless.sampler(d.occlusion.sampler);
    g.emissiveSampler = m_bindless.sampler(d.emissiveTexture.sampler);
    g.flags = (d.alphaMode == AlphaMode::Mask ? kMaterialAlphaMask : 0u) |
              (d.alphaMode == AlphaMode::Blend ? kMaterialAlphaBlend : 0u) |
              (d.doubleSided ? kMaterialDoubleSided : 0u);
    g.custom0 = d.custom0;
    g.custom1 = d.custom1;
    return g;
}

MaterialHandle MaterialSystem::createMaterial(const MaterialDesc& desc) {
    ZODAR_ENSURE(desc.shader < m_shaders.size(), "material '" + desc.name + "' uses an unknown shader");
    if (m_materials.size() >= kMaxMaterials) {
        fail("Material table is full");
    }
    const GpuMaterial gpu = toGpu(desc);
    const auto handle = static_cast<MaterialHandle>(m_materials.size());
    m_materialBuffer.write(&gpu, sizeof(gpu), static_cast<VkDeviceSize>(handle) * sizeof(GpuMaterial));
    m_materials.push_back(desc);
    return handle;
}

const GraphicsPipeline& MaterialSystem::pipeline(MaterialHandle handle) {
    const MaterialDesc& m = m_materials.at(handle);
    const uint64_t key = (static_cast<uint64_t>(m.shader) << 8) | (static_cast<uint64_t>(m.alphaMode) << 1) |
                         (m.doubleSided ? 1u : 0u);
    if (auto it = m_pipelines.find(key); it != m_pipelines.end()) {
        return *it->second;
    }
    const Shader& shader = m_shaders[m.shader];
    GraphicsPipelineDesc desc;
    desc.vertexShader = shader.vertex->handle();
    desc.fragmentShader = shader.fragment->handle();
    desc.vertexInput = VertexInput::EngineVertex;
    desc.colorFormat = m_formats.color;
    desc.depthFormat = m_formats.depth;
    desc.depthTest = true;
    desc.depthWrite = m.alphaMode != AlphaMode::Blend;
    desc.blend = m.alphaMode == AlphaMode::Blend ? BlendMode::AlphaBlend : BlendMode::Opaque;
    desc.cullMode = m.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    desc.sharedLayout = m_layout;
    const std::string debugName = shader.name + (m.alphaMode == AlphaMode::Blend ? "/blend" : "/opaque") +
                                  (m.doubleSided ? "/2s" : "");
    desc.debugName = debugName.c_str();
    auto pipeline = std::make_unique<GraphicsPipeline>(m_context, desc);
    ZODAR_LOG_DEBUG("Created pipeline {}", debugName);
    return *m_pipelines.emplace(key, std::move(pipeline)).first->second;
}

const GraphicsPipeline& MaterialSystem::depthPipeline(MaterialHandle handle) {
    const MaterialDesc& m = m_materials.at(handle);
    ZODAR_ENSURE(m.alphaMode != AlphaMode::Blend, "blended materials have no depth prepass");
    const bool masked = m.alphaMode == AlphaMode::Mask;
    const uint64_t key = (static_cast<uint64_t>(m.shader) << 8) | (masked ? 2u : 0u) | (m.doubleSided ? 1u : 0u);
    if (auto it = m_depthPipelines.find(key); it != m_depthPipelines.end()) {
        return *it->second;
    }
    const Shader& shader = m_shaders[m.shader];
    GraphicsPipelineDesc desc;
    desc.vertexShader = shader.vertex->handle();
    desc.fragmentShader = masked ? m_depthMask->handle() : VK_NULL_HANDLE;
    desc.vertexInput = VertexInput::EngineVertex;
    desc.colorFormat = VK_FORMAT_UNDEFINED;
    desc.depthFormat = m_formats.depth;
    desc.depthTest = true;
    desc.depthWrite = true;
    desc.cullMode = m.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    desc.sharedLayout = m_layout;
    const std::string debugName = shader.name + "/depth" + (masked ? "/mask" : "") + (m.doubleSided ? "/2s" : "");
    desc.debugName = debugName.c_str();
    return *m_depthPipelines.emplace(key, std::make_unique<GraphicsPipeline>(m_context, desc)).first->second;
}

} // namespace zodar::gfx
