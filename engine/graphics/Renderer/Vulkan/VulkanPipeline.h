#pragma once

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace zodar::gfx {

class VulkanContext;

// Reads a SPIR-V binary. Fails with a descriptive error when missing or malformed.
std::vector<uint32_t> loadSpirv(const std::filesystem::path& path);

class ShaderModule {
public:
    ShaderModule(VkDevice device, const std::vector<uint32_t>& spirv);
    ~ShaderModule();

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    VkShaderModule handle() const { return m_module; }

private:
    VkDevice m_device;
    VkShaderModule m_module = VK_NULL_HANDLE;
};

enum class VertexInput {
    None,         // Vertices generated in the shader (fullscreen passes).
    EngineVertex, // zodar::Vertex at binding 0: position, normal, tangent, uv0, color (locations 0-4).
};

enum class BlendMode {
    Opaque,
    AlphaBlend,      // src * a + dst * (1 - a)
    PremultipliedAlpha,
    Additive,
};

// Graphics pipeline for dynamic rendering. Viewport and scissor are always dynamic.
struct GraphicsPipelineDesc {
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    VertexInput vertexInput = VertexInput::None;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED; // UNDEFINED = no depth attachment.
    bool depthTest = false;
    bool depthWrite = false;
    VkCompareOp depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL; // Engine uses reverse Z.
    BlendMode blend = BlendMode::Opaque;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    // Either pass an existing layout (shared by many pipelines) or let the pipeline own one
    // built from setLayouts + push constants.
    VkPipelineLayout sharedLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> setLayouts;
    uint32_t pushConstantSize = 0;
    VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const char* debugName = nullptr;
};

class GraphicsPipeline {
public:
    GraphicsPipeline(const VulkanContext& context, const GraphicsPipelineDesc& desc);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }
    VkShaderStageFlags pushConstantStages() const { return m_pushConstantStages; }

private:
    VkDevice m_device;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    bool m_ownsLayout = false;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkShaderStageFlags m_pushConstantStages = 0;
};

// Compute pipeline with an owned layout (descriptor set layouts + one push-constant range).
class ComputePipeline {
public:
    ComputePipeline(const VulkanContext& context, const std::vector<uint32_t>& spirv,
                    const std::vector<VkDescriptorSetLayout>& setLayouts, uint32_t pushConstantSize,
                    const char* debugName = nullptr);
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    VkPipeline handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }

private:
    VkDevice m_device;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace zodar::gfx
