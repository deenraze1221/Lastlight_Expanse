#pragma once

#include "Renderer/Resources/GpuBuffer.h"
#include "Renderer/Resources/GpuImage.h"
#include "Renderer/Resources/UploadQueue.h"

#include <volk.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
#include <algorithm>

namespace zodar::gfx {

class VulkanContext;

// One mip level of tightly packed texel data (rows of blocks for block-compressed formats).
struct ImageLevelData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::span<const uint8_t> bytes;
};

// Bytes per texel for uncompressed formats used by the engine; 0 if unsupported.
uint32_t formatBytesPerPixel(VkFormat format);

// Texel block of a format: 1x1 for uncompressed formats, 4x4 for BCn. bytes == 0: unsupported.
struct FormatBlock {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t bytes = 0;
};
FormatBlock formatBlock(VkFormat format);
// Bytes of one tightly packed level (whole blocks); 0 for unsupported formats.
VkDeviceSize imageLevelSize(VkFormat format, uint32_t width, uint32_t height);

// GPU uploads and readbacks.
//
// Uploads (UploadBatch) run on the UploadQueue: the dedicated transfer queue when the device
// has one, else the graphics queue. readImage/readBuffer/immediate() record on the graphics
// queue and block (loading, tests, tools). Use from the thread that submits frames.
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
    std::vector<uint8_t> readImage(const GpuImage& image, VkImageLayout layout, uint32_t mip = 0, uint32_t layer = 0);
    std::vector<uint8_t> readBuffer(const GpuBuffer& buffer);

    const VulkanContext& context() const { return m_context; }
    UploadQueue& queue() { return *m_queue; }
    const UploadQueue& queue() const { return *m_queue; }

private:
    friend class UploadBatch;

    VulkanContext& m_context;
    std::unique_ptr<UploadQueue> m_queue;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    bool m_recording = false;
};

// Collects many uploads into one transfer submission. Staging memory lives until the
// transfer has finished.
//   submit():      blocking; resources are usable by graphics work when it returns.
//   submitAsync(): returns immediately; resources are usable once the ticket is ready
//                  (UploadQueue::isReady, normally after the frame loop's acquireCompleted).
// Resources created by a batch must not be used by the GPU before that.
class UploadBatch {
public:
    explicit UploadBatch(UploadContext& upload);
    ~UploadBatch();

    UploadBatch(const UploadBatch&) = delete;
    UploadBatch& operator=(const UploadBatch&) = delete;

    // Creates a device-local buffer initialised with `data`.
    GpuBuffer createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                           const char* debugName = nullptr);

    // Copies `data` into an existing buffer (TRANSFER_DST usage) at `offset`. The graphics
    // queue may use other ranges meanwhile, so with a dedicated transfer queue the buffer
    // must be created with BufferDesc::sharedWithTransfer (or by this batch).
    void copyToBuffer(const void* data, VkDeviceSize size, const GpuBuffer& target, VkDeviceSize offset);

    // Appends custom commands, executed in order with the batch's other commands, on the
    // upload queue (transfer operations only). Buffers they touch follow the copyToBuffer rule.
    void record(std::function<void(VkCommandBuffer)> commands);

    // Keeps `object` alive until the transfer has finished and the graphics queue is done
    // with it (e.g. a buffer the batch copies from that is being replaced).
    void keepAlive(std::shared_ptr<void> object);

    // Uploads all provided mip levels (level i goes to mip i) and hands the image to the
    // graphics queue in SHADER_READ_ONLY_OPTIMAL. The image must not have been used yet and
    // have at least levels.size() mips. Block-compressed formats take whole 4x4 blocks.
    void uploadImage(const GpuImage& image, std::span<const ImageLevelData> levels);

    // Submits and waits. Called automatically by the destructor if neither submit function
    // was called (errors are then logged instead of thrown).
    void submit();
    UploadTicket submitAsync();
    // Bytes staged so far.
    VkDeviceSize stagingBytes() const { return m_work.stagingBytes; }

    const VulkanContext& context() const { return m_upload.context(); }

private:
    GpuBuffer& stage(const void* data, VkDeviceSize size);

    UploadContext& m_upload;
    UploadQueue::Work m_work;
    bool m_submitted = false;
};

} // namespace zodar::gfx
