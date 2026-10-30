#pragma once

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace raven::gfx {

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

// Minimal description for Milestone 1: one colour attachment, no vertex input,
// dynamic viewport/scissor, dynamic rendering. Vertex layouts, depth, blending and
// descriptor sets are added with the Milestone 2 mesh and material work.
struct GraphicsPipelineDesc {
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    uint32_t pushConstantSize = 0;
    VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
};

class GraphicsPipeline {
public:
    GraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& desc);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }
    VkShaderStageFlags pushConstantStages() const { return m_pushConstantStages; }

private:
    VkDevice m_device;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkShaderStageFlags m_pushConstantStages = 0;
};

} // namespace raven::gfx
