#pragma once

#include <volk.h>

namespace zodar::gfx {

// How a pass uses a resource. Each usage maps to a synchronization2 stage/access mask
// and (for images) a layout.
enum class ResourceUsage {
    ColorAttachment,     // Write (raster)
    DepthAttachment,     // Read + write (depth test and depth write)
    DepthRead,           // Depth test without writes; image stays in a read-only depth layout
    SampledFragment,     // Texture read in fragment shaders
    SampledCompute,      // Texture read in compute shaders
    SampledGraphics,     // Texture read in any graphics stage
    StorageRead,         // Storage image/buffer read (compute)
    StorageWrite,        // Storage image/buffer write (compute)
    StorageReadWrite,    // Storage read-modify-write (compute)
    TransferSrc,
    TransferDst,
    IndirectArgs,        // Buffer read by vkCmdDraw*Indirect*
    VertexInput,         // Vertex/index buffer
    UniformRead,         // Uniform/storage buffer read by graphics shaders
};

struct AccessState {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

bool isWriteUsage(ResourceUsage usage);
AccessState usageState(ResourceUsage usage);
VkImageUsageFlags imageUsageFlags(ResourceUsage usage);
const char* toString(ResourceUsage usage);

} // namespace zodar::gfx
