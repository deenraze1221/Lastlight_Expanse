#include "Renderer/Material.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Resources/DeletionQueue.h"
#include "Renderer/Texture.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <algorithm>
#include <system_error>

namespace zodar::gfx {
namespace {

// Pipeline cache keys: colour = shader << 8 | alphaMode << 1 | doubleSided,
// depth = shader << 8 | masked << 1 | doubleSided.
uint64_t colorKey(ShaderHandle shader, AlphaMode alpha, bool doubleSided) {
    return (static_cast<uint64_t>(shader) << 8) | (static_cast<uint64_t>(alpha) << 1) | (doubleSided ? 1u : 0u);
}
uint64_t depthKey(ShaderHandle shader, bool masked, bool doubleSided) {
    return (static_cast<uint64_t>(shader) << 8) | (masked ? 2u : 0u) | (doubleSided ? 1u : 0u);
}

std::filesystem::path normalised(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path full = std::filesystem::weakly_canonical(std::filesystem::absolute(p, ec), ec);
    return ec ? p.lexically_normal() : full;
}

// Fails unless `words` (already header-checked by loadSpirv) declares an entry point of the
// given execution model (SPIR-V: 0 = Vertex, 4 = Fragment) and every instruction is in range.
void checkSpirvStage(const std::vector<uint32_t>& words, uint32_t executionModel, const std::filesystem::path& file) {
    constexpr uint32_t kOpEntryPoint = 15;
    bool found = false;
    for (size_t i = 5; i < words.size();) {
        const uint32_t count = words[i] >> 16;
        const uint32_t opcode = words[i] & 0xffffu;
        if (count == 0 || i + count > words.size()) {
            fail("Shader '" + file.string() + "' is truncated or malformed (instruction at word " + std::to_string(i) + ")");
        }
        if (opcode == kOpEntryPoint && count >= 2 && words[i + 1] == executionModel) {
            found = true;
        }
        i += count;
    }
    if (!found) {
        fail("Shader '" + file.string() + "' has no " + (executionModel == 0 ? "vertex" : "fragment") +
             " entry point");
    }
}

} // namespace

MaterialSystem::MaterialSystem(const VulkanContext& context, BindlessRegistry& bindless, VkPipelineLayout layout,
                               TargetFormats formats, DefaultTextureSlots defaults,
                               const std::filesystem::path& shaderDirectory)
    : m_context(context), m_bindless(bindless), m_layout(layout), m_formats(formats), m_defaults(defaults),
      m_shaderDirectory(shaderDirectory) {
    // Host-visible table: a new material's slot is never read by in-flight frames.
    m_materialBuffer = GpuBuffer(context, {sizeof(GpuMaterial) * kMaxMaterials, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           MemoryUsage::Upload, "MaterialTable"});
    m_bindless.setMaterialBuffer(m_materialBuffer.handle(), m_materialBuffer.size());

    m_pbr = registerShader(kPbrShader, "mesh.vert.spv", "pbr.frag.spv");
    m_unlit = registerShader(kUnlitShader, "mesh.vert.spv", "unlit.frag.spv");
    m_voxel = registerShader(kVoxelShader, "mesh.vert.spv", "voxel_pbr.frag.spv");
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
    const auto resolve = [&](const std::filesystem::path& p) {
        return normalised(p.is_absolute() ? p : m_shaderDirectory / p);
    };
    Shader shader;
    shader.name = name;
    shader.vertexFile = resolve(vertexSpv);
    shader.fragmentFile = resolve(fragmentSpv);
    shader.vertex = std::make_unique<ShaderModule>(m_context.device(), loadSpirv(shader.vertexFile));
    shader.fragment = std::make_unique<ShaderModule>(m_context.device(), loadSpirv(shader.fragmentFile));
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
    if (!m_free.empty()) {
        // The slot is no longer read by the GPU (releaseMaterial contract): write it directly.
        const MaterialHandle handle = m_free.back();
        m_free.pop_back();
        m_materialBuffer.write(&gpu, sizeof(gpu), static_cast<VkDeviceSize>(handle) * sizeof(GpuMaterial));
        m_materials[handle] = desc;
        return handle;
    }
    const auto handle = static_cast<MaterialHandle>(m_materials.size());
    m_materialBuffer.write(&gpu, sizeof(gpu), static_cast<VkDeviceSize>(handle) * sizeof(GpuMaterial));
    m_materials.push_back(desc);
    return handle;
}

void MaterialSystem::releaseMaterial(MaterialHandle handle) {
    ZODAR_ENSURE(handle < m_materials.size(), "releaseMaterial: unknown material handle " + std::to_string(handle));
    ZODAR_ENSURE(std::find(m_free.begin(), m_free.end(), handle) == m_free.end(),
                 "releaseMaterial: material handle " + std::to_string(handle) + " was already released");
    std::erase(m_pending, handle);
    m_materials[handle].name += " (released)";
    m_free.push_back(handle);
}

void MaterialSystem::updateMaterial(MaterialHandle handle, const MaterialDesc& desc) {
    ZODAR_ENSURE(handle < m_materials.size(), "updateMaterial: unknown material handle " + std::to_string(handle));
    MaterialDesc& current = m_materials[handle];
    if (desc.shader != current.shader || desc.alphaMode != current.alphaMode || desc.doubleSided != current.doubleSided) {
        fail("updateMaterial: material '" + current.name +
             "' cannot change shader, alphaMode or doubleSided (pipeline state); create a new material");
    }
    current = desc;
    if (std::find(m_pending.begin(), m_pending.end(), handle) == m_pending.end()) {
        m_pending.push_back(handle);
    }
}

void MaterialSystem::recordPendingUpdates(VkCommandBuffer cmd) {
    if (m_pending.empty()) {
        return;
    }
    // Earlier submissions (frames in flight) may still read the table: a barrier's first scope
    // covers all previously submitted commands on the queue, so the writes wait for them.
    VkMemoryBarrier2 before{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    before.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    before.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    before.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    before.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &before;
    vkCmdPipelineBarrier2(cmd, &dep);
    for (const MaterialHandle handle : m_pending) {
        const GpuMaterial gpu = toGpu(m_materials[handle]);
        vkCmdUpdateBuffer(cmd, m_materialBuffer.handle(), static_cast<VkDeviceSize>(handle) * sizeof(GpuMaterial),
                          sizeof(GpuMaterial), &gpu);
    }
    VkMemoryBarrier2 after{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    after.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    after.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    after.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    after.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    dep.pMemoryBarriers = &after;
    vkCmdPipelineBarrier2(cmd, &dep);
    m_pending.clear();
}

const GraphicsPipeline& MaterialSystem::pipeline(MaterialHandle handle) {
    const MaterialDesc& m = m_materials.at(handle);
    const uint64_t key = colorKey(m.shader, m.alphaMode, m.doubleSided);
    if (auto it = m_pipelines.find(key); it != m_pipelines.end()) {
        return *it->second;
    }
    return *m_pipelines.emplace(key, buildPipeline(m_shaders[m.shader], key)).first->second;
}

std::unique_ptr<GraphicsPipeline> MaterialSystem::buildPipeline(const Shader& shader, uint64_t key) const {
    struct {
        AlphaMode alphaMode;
        bool doubleSided;
    } m{static_cast<AlphaMode>((key >> 1) & 0x7fu), (key & 1u) != 0};
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
    return pipeline;
}

const GraphicsPipeline& MaterialSystem::depthPipeline(MaterialHandle handle) {
    const MaterialDesc& m = m_materials.at(handle);
    ZODAR_ENSURE(m.alphaMode != AlphaMode::Blend, "blended materials have no depth prepass");
    const uint64_t key = depthKey(m.shader, m.alphaMode == AlphaMode::Mask, m.doubleSided);
    if (auto it = m_depthPipelines.find(key); it != m_depthPipelines.end()) {
        return *it->second;
    }
    return *m_depthPipelines.emplace(key, buildDepthPipeline(m_shaders[m.shader], key)).first->second;
}

std::unique_ptr<GraphicsPipeline> MaterialSystem::buildDepthPipeline(const Shader& shader, uint64_t key) const {
    const bool masked = (key & 2u) != 0;
    struct {
        bool doubleSided;
    } m{(key & 1u) != 0};
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
    return std::make_unique<GraphicsPipeline>(m_context, desc);
}

ShaderReloadResult MaterialSystem::reloadShader(ShaderHandle handle, DeletionQueue& retire) {
    ShaderReloadResult result;
    if (handle >= m_shaders.size()) {
        result.error = "unknown shader handle " + std::to_string(handle);
        return result;
    }
    Shader& shader = m_shaders[handle];
    // Everything new is built first; on any exception it is destroyed unused.
    struct Retired {
        std::vector<std::unique_ptr<GraphicsPipeline>> pipelines;
        std::unique_ptr<ShaderModule> vertex;
        std::unique_ptr<ShaderModule> fragment;
    };
    auto retired = std::make_shared<Retired>();
    std::vector<GraphicsPipeline*> targets;
    try {
        Shader fresh;
        fresh.name = shader.name;
        const std::vector<uint32_t> vertex = loadSpirv(shader.vertexFile);
        checkSpirvStage(vertex, 0, shader.vertexFile);
        const std::vector<uint32_t> fragment = loadSpirv(shader.fragmentFile);
        checkSpirvStage(fragment, 4, shader.fragmentFile);
        fresh.vertex = std::make_unique<ShaderModule>(m_context.device(), vertex);
        fresh.fragment = std::make_unique<ShaderModule>(m_context.device(), fragment);
        for (auto& [key, pipeline] : m_pipelines) {
            if ((key >> 8) == handle) {
                retired->pipelines.push_back(buildPipeline(fresh, key));
                targets.push_back(pipeline.get());
            }
        }
        for (auto& [key, pipeline] : m_depthPipelines) {
            if ((key >> 8) == handle) {
                retired->pipelines.push_back(buildDepthPipeline(fresh, key));
                targets.push_back(pipeline.get());
            }
        }
        // Commit: new objects into the existing pipelines/shader, old ones to `retire`.
        for (size_t i = 0; i < targets.size(); ++i) {
            targets[i]->swap(*retired->pipelines[i]);
        }
        retired->vertex = std::exchange(shader.vertex, std::move(fresh.vertex));
        retired->fragment = std::exchange(shader.fragment, std::move(fresh.fragment));
    } catch (const std::exception& e) {
        result.error = e.what();
        return result;
    }
    retire.retire([retired]() mutable { retired.reset(); });
    result.ok = true;
    result.pipelinesRebuilt = static_cast<uint32_t>(targets.size());
    ZODAR_LOG_INFO("Shader '{}' reloaded ({} pipelines rebuilt)", shader.name, result.pipelinesRebuilt);
    return result;
}

} // namespace zodar::gfx
