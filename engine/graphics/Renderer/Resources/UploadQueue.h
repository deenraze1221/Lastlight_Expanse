#pragma once

#include "Renderer/Resources/GpuBuffer.h"

#include <volk.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace zodar::gfx {

class VulkanContext;

// Identifies one upload submission. Values increase monotonically; 0 is "no upload".
struct UploadTicket {
    uint64_t value = 0;
    bool valid() const { return value != 0; }
};

// An image written by an upload, with the layout the graphics queue receives it in.
struct UploadedImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageSubresourceRange range{};
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

// Barriers that hand uploaded resources from the transfer queue family to the graphics
// family. With one family, `release` only performs the final layout transitions and
// `acquire` is empty. Pure function of its inputs (unit tested without a GPU).
struct OwnershipTransfer {
    std::vector<VkImageMemoryBarrier2> releaseImages, acquireImages;
    std::vector<VkBufferMemoryBarrier2> releaseBuffers, acquireBuffers;
    bool needsAcquire() const { return !acquireImages.empty() || !acquireBuffers.empty(); }
};
// `images` must be in TRANSFER_DST_OPTIMAL after the upload commands. `exclusiveBuffers` are
// buffers in VK_SHARING_MODE_EXCLUSIVE written by the upload (concurrent ones need nothing).
OwnershipTransfer makeOwnershipTransfer(std::span<const UploadedImage> images,
                                        std::span<const VkBuffer> exclusiveBuffers, uint32_t transferFamily,
                                        uint32_t graphicsFamily);
void recordBarriers(VkCommandBuffer cmd, std::span<const VkImageMemoryBarrier2> images,
                    std::span<const VkBufferMemoryBarrier2> buffers);

// Asynchronous uploads on the dedicated transfer queue (fallback: the graphics queue).
//
// submit() records and submits a batch and returns immediately with a ticket; the
// transfer signals a timeline semaphore. Before the graphics queue may use the uploaded
// resources the ticket must be *acquired*:
//   - per frame (non-blocking): acquireCompleted(frameCmd) records the queue-family acquire
//     barriers of every finished upload into the frame's command buffer and makes that
//     submission wait on the transfer timeline (already signalled, so it never stalls);
//   - on demand (blocking): wait(ticket) waits on the host and submits the acquire itself.
// isReady(ticket) is true once acquired; resources may then be used by graphics commands
// recorded after that point. Staging memory is released when the transfer has finished.
//
// Not thread-safe; use from the thread that submits frames.
class UploadQueue {
public:
    struct Work {
        std::vector<std::function<void(VkCommandBuffer)>> commands;
        std::vector<GpuBuffer> staging;
        std::vector<UploadedImage> images;
        std::vector<VkBuffer> exclusiveBuffers;
        std::vector<std::shared_ptr<void>> keepAlive; // Released after the transfer, via the deletion queue.
        VkDeviceSize stagingBytes = 0;
    };

    struct Stats {
        uint64_t submitted = 0;
        uint64_t completed = 0; // Transfer finished (acquired or not).
        uint32_t inFlight = 0;  // Transfer not finished yet.
        VkDeviceSize stagingBytesInFlight = 0;
        VkDeviceSize bytesUploaded = 0;
    };

    explicit UploadQueue(VulkanContext& context);
    ~UploadQueue();

    UploadQueue(const UploadQueue&) = delete;
    UploadQueue& operator=(const UploadQueue&) = delete;

    // True when uploads run on their own queue (dedicated transfer family).
    bool dedicated() const { return m_dedicated; }
    uint32_t family() const { return m_family; }

    UploadTicket submit(Work&& work);

    bool isTransferComplete(UploadTicket ticket) const;
    bool isReady(UploadTicket ticket) const;
    // Non-blocking. Returns the number of uploads acquired into `graphicsCmd`.
    uint32_t acquireCompleted(VkCommandBuffer graphicsCmd);
    // Blocking: the ticket is ready when this returns.
    void wait(UploadTicket ticket);
    void waitIdle();

    Stats stats() const;
    VkSemaphore timeline() const { return m_timeline; }

private:
    enum class State { InFlight, TransferDone, Acquired };
    struct Submission {
        uint64_t value = 0;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Work work;
        OwnershipTransfer ownership;
        State state = State::InFlight;
    };
    struct AcquireCmd {
        VkCommandBuffer cmd;
        uint64_t graphicsValue;
    };

    void poll();                    // InFlight -> TransferDone for finished fences
    void finishTransfer(Submission& s);
    void prune();                   // Drops acquired submissions from the front
    Submission* find(uint64_t value);
    const Submission* find(uint64_t value) const;
    VkCommandBuffer takeCommandBuffer();
    VkFence takeFence();

    VulkanContext& m_context;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_family = 0;
    uint32_t m_graphicsFamily = 0;
    bool m_dedicated = false;
    VkSemaphore m_timeline = VK_NULL_HANDLE;
    uint64_t m_nextValue = 0;
    uint64_t m_prunedUpTo = 0; // Every ticket <= this is acquired.

    VkCommandPool m_pool = VK_NULL_HANDLE;         // Transfer family
    VkCommandPool m_acquirePool = VK_NULL_HANDLE;  // Graphics family (blocking acquires)
    std::vector<VkCommandBuffer> m_freeCmds;
    std::vector<VkFence> m_freeFences;
    std::vector<AcquireCmd> m_acquireCmds;
    std::vector<VkCommandBuffer> m_freeAcquireCmds;
    std::deque<Submission> m_submissions;
    Stats m_stats;
};

} // namespace zodar::gfx
