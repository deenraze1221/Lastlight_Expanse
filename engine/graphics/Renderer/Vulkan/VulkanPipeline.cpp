#include "Renderer/Vulkan/VulkanPipeline.h"

#include "Core/Error.h"
#include "Assets/MeshData.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <cstddef>

#include <fstream>

namespace zodar::gfx {

std::vector<uint32_t> loadSpirv(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        fail("Cannot open shader '" + path.string() + "'");
    }
    const auto size = static_cast<size_t>(file.tellg());
    constexpr uint32_t kSpirvMagic = 0x07230203;
    if (size < 20 || size % 4 != 0) {
        fail("Shader '" + path.string() + "' is not a valid SPIR-V binary (size " + std::to_string(size) + ")");
    }
    std::vector<uint32_t> words(size / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
    if (!file || words[0] != kSpirvMagic) {
        fail("Shader '" + path.string() + "' is not a valid SPIR-V binary");
    }
    return words;
}

ShaderModule::ShaderModule(VkDevice device, const std::vector<uint32_t>& spirv) : m_device(device) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size() * sizeof(uint32_t);
    info.pCode = spirv.data();
    ZODAR_VK_CHECK(vkCreateShaderModule(m_device, &info, nullptr, &m_module));
}

ShaderModule::~ShaderModule() {
    vkDestroyShaderModule(m_device, m_module, nullptr);
}

GraphicsPipeline::GraphicsPipeline(const VulkanContext& context, const GraphicsPipelineDesc& desc)
    : m_device(context.device()), m_pushConstantStages(desc.pushConstantStages) {
    // Depth-only pipelines (prepass, shadows) have no colour attachment and may omit the
    // fragment shader.
    const bool depthOnly = desc.colorFormat == VK_FORMAT_UNDEFINED;
    ZODAR_ENSURE(desc.vertexShader && (desc.fragmentShader || depthOnly), "pipeline needs vertex and fragment shaders");
    ZODAR_ENSURE(!depthOnly || desc.depthFormat != VK_FORMAT_UNDEFINED, "pipeline needs a colour or depth format");

    if (desc.sharedLayout) {
        m_layout = desc.sharedLayout;
    } else {
        VkPushConstantRange range{desc.pushConstantStages, 0, desc.pushConstantSize};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(desc.setLayouts.size());
        layoutInfo.pSetLayouts = desc.setLayouts.data();
        layoutInfo.pushConstantRangeCount = desc.pushConstantSize > 0 ? 1 : 0;
        layoutInfo.pPushConstantRanges = &range;
        ZODAR_VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_layout));
        m_ownsLayout = true;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = desc.vertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = desc.fragmentShader;
    stages[1].pName = "main";

    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv0)},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (desc.vertexInput == VertexInput::EngineVertex) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 5;
        vertexInput.pVertexAttributeDescriptions = attributes;
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = desc.topology;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cullMode;
    raster.frontFace = desc.frontFace;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = desc.depthCompare;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.blend != BlendMode::Opaque) {
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        switch (desc.blend) {
        case BlendMode::AlphaBlend:
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BlendMode::PremultipliedAlpha:
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BlendMode::Additive:
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case BlendMode::Opaque:
            break;
        }
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = depthOnly ? 0 : 1;
    blend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = depthOnly ? 0 : 1;
    rendering.pColorAttachmentFormats = &desc.colorFormat;
    rendering.depthAttachmentFormat = desc.depthFormat;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = desc.fragmentShader ? 2 : 1;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = m_layout;

    const VkResult result = vkCreateGraphicsPipelines(m_device, context.pipelineCache(), 1, &info, nullptr, &m_pipeline);
    if (result != VK_SUCCESS) {
        if (m_ownsLayout) {
            vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        }
        throwVkError(result, "vkCreateGraphicsPipelines");
    }
    context.setDebugName(m_pipeline, VK_OBJECT_TYPE_PIPELINE, desc.debugName);
}

void GraphicsPipeline::swap(GraphicsPipeline& other) noexcept {
    std::swap(m_device, other.m_device);
    std::swap(m_layout, other.m_layout);
    std::swap(m_ownsLayout, other.m_ownsLayout);
    std::swap(m_pipeline, other.m_pipeline);
    std::swap(m_pushConstantStages, other.m_pushConstantStages);
}

GraphicsPipeline::~GraphicsPipeline() {
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_ownsLayout) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    }
}

ComputePipeline::ComputePipeline(const VulkanContext& context, const std::vector<uint32_t>& spirv,
                                 const std::vector<VkDescriptorSetLayout>& setLayouts, uint32_t pushConstantSize,
                                 const char* debugName)
    : m_device(context.device()) {
    const ShaderModule module(m_device, spirv);
    const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSize};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;
    layoutInfo.pPushConstantRanges = &range;
    ZODAR_VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_layout));

    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module.handle();
    info.stage.pName = "main";
    info.layout = m_layout;
    const VkResult result = vkCreateComputePipelines(m_device, context.pipelineCache(), 1, &info, nullptr, &m_pipeline);
    if (result != VK_SUCCESS) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        throwVkError(result, "vkCreateComputePipelines");
    }
    context.setDebugName(m_pipeline, VK_OBJECT_TYPE_PIPELINE, debugName);
}

ComputePipeline::~ComputePipeline() {
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_layout, nullptr);
}

} // namespace zodar::gfx
