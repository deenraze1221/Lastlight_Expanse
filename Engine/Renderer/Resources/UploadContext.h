#pragma once

#include "Renderer/Resources/GpuBuffer.h"
#include "Renderer/Resources/GpuImage.h"

#include <volk.h>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>
#include <algorithm>

namespace zodar::gfx {

class VulkanContext;

// One mip level of tightly packed pixel data.
struct ImageLevelData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::span<const uint8_t> bytes;
};

// Bytes per texel for uncompressed formats used by the engine; 0 if unsupported.
uint32_t formatBytesPerPixel(VkFormat format);

// Synchronous GPU work submission for loading and readback. Uses the graphics queue on
// the calling thread; it must not be used concurrently with frame submission from another
// thread. Asynchronous streaming (dedicated transfer queue) is a later milestone.
class UploadContext {
public:
    explicit UploadContext(VulkanContext& context);
    ~UploadContext();

    UploadContext(const UploadContext&) = delete;
    UploadContext& operator=(const UploadContext&) = delete;

    // Records commands with the callback, submits and blocks until complete.
    void immediate(const std::function<void(VkCommandBuffer)>& record);

    // Copies an image mip into host memory (debugging/tests). The image is returned to
    // `layout` afterwards.
    std::vector<uint8_t> readImage(const GpuImage& image, VkImageLayout layout, uint32_t mip = 0);
    std::vector<uint8_t> readBuffer(const GpuBuffer& buffer);

    const VulkanContext& context() const { return m_context; }

private:
    friend class UploadBatch;

    VulkanContext& m_context;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    bool m_recording = false;
};

// Collects many uploads into one submission. Staging memory lives until submit().
// Returned resources may only be used after submit() has returned.
class UploadBatch {
public:
    explicit UploadBatch(UploadContext& upload);
    ~UploadBatch();

    UploadBatch(const UploadBatch&) = delete;
    UploadBatch& operator=(const UploadBatch&) = delete;

    // Creates a device-local buffer initialised with `data`.
    GpuBuffer createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                           const char* debugName = nullptr);

    // Uploads all provided mip levels (level i goes to mip i) and leaves the image in
    // SHADER_READ_ONLY_OPTIMAL. The image must have at least levels.size() mips and
    // exactly that many are expected to be written.
    void uploadImage(const GpuImage& image, std::span<const ImageLevelData> levels);

    // Submits and waits. Called automatically by the destructor if omitted (errors are
    // then logged instead of thrown).
    void submit();

    const VulkanContext& context() const { return m_upload.context(); }

private:
    UploadContext& m_upload;
    std::vector<GpuBuffer> m_staging;
    std::vector<std::function<void(VkCommandBuffer)>> m_commands;
    bool m_submitted = false;
};

} // namespace zodar::gfx
