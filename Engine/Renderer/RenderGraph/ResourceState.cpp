#include "Renderer/RenderGraph/ResourceState.h"

namespace zodar::gfx {

bool isWriteUsage(ResourceUsage u) {
    switch (u) {
    case ResourceUsage::ColorAttachment:
    case ResourceUsage::DepthAttachment:
    case ResourceUsage::StorageWrite:
    case ResourceUsage::StorageReadWrite:
    case ResourceUsage::TransferDst:
        return true;
    default:
        return false;
    }
}

AccessState usageState(ResourceUsage u) {
    constexpr VkPipelineStageFlags2 kDepthStages =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    constexpr VkPipelineStageFlags2 kGraphicsShaders =
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    switch (u) {
    case ResourceUsage::ColorAttachment:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case ResourceUsage::DepthAttachment:
        return {kDepthStages,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
    case ResourceUsage::DepthRead:
        return {kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
    case ResourceUsage::SampledFragment:
        return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ResourceUsage::SampledCompute:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ResourceUsage::SampledGraphics:
        return {kGraphicsShaders, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ResourceUsage::StorageRead:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case ResourceUsage::StorageWrite:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case ResourceUsage::StorageReadWrite:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case ResourceUsage::TransferSrc:
        return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case ResourceUsage::TransferDst:
        return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case ResourceUsage::IndirectArgs:
        return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceUsage::VertexInput:
        return {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceUsage::UniformRead:
        return {kGraphicsShaders | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    return {};
}

VkImageUsageFlags imageUsageFlags(ResourceUsage u) {
    switch (u) {
    case ResourceUsage::ColorAttachment: return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    case ResourceUsage::DepthAttachment:
    case ResourceUsage::DepthRead: return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    case ResourceUsage::SampledFragment:
    case ResourceUsage::SampledCompute:
    case ResourceUsage::SampledGraphics: return VK_IMAGE_USAGE_SAMPLED_BIT;
    case ResourceUsage::StorageRead:
    case ResourceUsage::StorageWrite:
    case ResourceUsage::StorageReadWrite: return VK_IMAGE_USAGE_STORAGE_BIT;
    case ResourceUsage::TransferSrc: return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    case ResourceUsage::TransferDst: return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    default: return 0;
    }
}

const char* toString(ResourceUsage u) {
    switch (u) {
    case ResourceUsage::ColorAttachment: return "ColorAttachment";
    case ResourceUsage::DepthAttachment: return "DepthAttachment";
    case ResourceUsage::DepthRead: return "DepthRead";
    case ResourceUsage::SampledFragment: return "SampledFragment";
    case ResourceUsage::SampledCompute: return "SampledCompute";
    case ResourceUsage::SampledGraphics: return "SampledGraphics";
    case ResourceUsage::StorageRead: return "StorageRead";
    case ResourceUsage::StorageWrite: return "StorageWrite";
    case ResourceUsage::StorageReadWrite: return "StorageReadWrite";
    case ResourceUsage::TransferSrc: return "TransferSrc";
    case ResourceUsage::TransferDst: return "TransferDst";
    case ResourceUsage::IndirectArgs: return "IndirectArgs";
    case ResourceUsage::VertexInput: return "VertexInput";
    case ResourceUsage::UniformRead: return "UniformRead";
    }
    return "?";
}

} // namespace zodar::gfx
