#include "Renderer/Resources/UploadQueue.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <algorithm>

namespace zodar::gfx {

OwnershipTransfer makeOwnershipTransfer(std::span<const UploadedImage> images,
                                        std::span<const VkBuffer> exclusiveBuffers, uint32_t transferFamily,
                                        uint32_t graphicsFamily) {
    OwnershipTransfer out;
    const bool cross = transferFamily != graphicsFamily;
    for (const UploadedImage& img : images) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = img.finalLayout;
        b.image = img.image;
        b.subresourceRange = img.range;
        if (cross) {
            // Release half: the destination scope is ignored for a release.
            b.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            b.dstAccessMask = VK_ACCESS_2_NONE;
            b.srcQueueFamilyIndex = transferFamily;
            b.dstQueueFamilyIndex = graphicsFamily;
            out.releaseImages.push_back(b);
            // Acquire half: same layouts and families; the source scope is ignored.
            b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            b.srcAccessMask = VK_ACCESS_2_NONE;
            b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
            out.acquireImages.push_back(b);
        } else {
            b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            out.releaseImages.push_back(b);
        }
    }
    if (cross) {
        for (const VkBuffer buffer : exclusiveBuffers) {
            VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b.srcQueueFamilyIndex = transferFamily;
            b.dstQueueFamilyIndex = graphicsFamily;
            b.buffer = buffer;
            b.offset = 0;
            b.size = VK_WHOLE_SIZE;
            out.releaseBuffers.push_back(b);
            b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            b.srcAccessMask = VK_ACCESS_2_NONE;
            b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
            out.acquireBuffers.push_back(b);
        }
    }
    // One family: buffer writes become visible through the timeline semaphore wait.
    return out;
}

void recordBarriers(VkCommandBuffer cmd, std::span<const VkImageMemoryBarrier2> images,
                    std::span<const VkBufferMemoryBarrier2> buffers) {
    if (images.empty() && buffers.empty()) {
        return;
    }
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(images.size());
    dep.pImageMemoryBarriers = images.data();
    dep.bufferMemoryBarrierCount = static_cast<uint32_t>(buffers.size());
    dep.pBufferMemoryBarriers = buffers.data();
    vkCmdPipelineBarrier2(cmd, &dep);
}

UploadQueue::UploadQueue(VulkanContext& context) : m_context(context) {
    const VkDevice device = context.device();
    m_graphicsFamily = context.graphicsQueueFamily();
    m_dedicated = context.transferQueue() != VK_NULL_HANDLE;
    m_queue = m_dedicated ? context.transferQueue() : context.graphicsQueue();
    m_family = context.transferQueueFamily();

    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semInfo.pNext = &type;
    ZODAR_VK_CHECK(vkCreateSemaphore(device, &semInfo, nullptr, &m_timeline));
    context.setDebugName(m_timeline, VK_OBJECT_TYPE_SEMAPHORE, "UploadQueue.timeline");

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_family;
    VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &m_pool);
    if (result == VK_SUCCESS) {
        poolInfo.queueFamilyIndex = m_graphicsFamily;
        result = vkCreateCommandPool(device, &poolInfo, nullptr, &m_acquirePool);
    }
    if (result != VK_SUCCESS) {
        vkDestroyCommandPool(device, m_pool, nullptr);
        vkDestroySemaphore(device, m_timeline, nullptr);
        throwVkError(result, "UploadQueue setup");
    }
    context.setDebugName(m_pool, VK_OBJECT_TYPE_COMMAND_POOL, "UploadQueue");
    context.setDebugName(m_acquirePool, VK_OBJECT_TYPE_COMMAND_POOL, "UploadQueue.acquire");
    ZODAR_LOG_INFO("Uploads: {} (family {})", m_dedicated ? "dedicated transfer queue" : "graphics queue (fallback)",
                   m_family);
}

UploadQueue::~UploadQueue() {
    // Uploads and blocking acquires may still be executing.
    vkDeviceWaitIdle(m_context.device());
    for (Submission& s : m_submissions) {
        if (s.state == State::InFlight) finishTransfer(s);
    }
    m_submissions.clear();
    m_context.removeGraphicsWait(m_timeline);
    const VkDevice device = m_context.device();
    for (VkFence f : m_freeFences) vkDestroyFence(device, f, nullptr);
    vkDestroyCommandPool(device, m_acquirePool, nullptr);
    vkDestroyCommandPool(device, m_pool, nullptr);
    vkDestroySemaphore(device, m_timeline, nullptr);
}

VkCommandBuffer UploadQueue::takeCommandBuffer() {
    if (!m_freeCmds.empty()) {
        const VkCommandBuffer cmd = m_freeCmds.back();
        m_freeCmds.pop_back();
        return cmd;
    }
    VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    info.commandPool = m_pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    ZODAR_VK_CHECK(vkAllocateCommandBuffers(m_context.device(), &info, &cmd));
    return cmd;
}

VkFence UploadQueue::takeFence() {
    if (!m_freeFences.empty()) {
        const VkFence fence = m_freeFences.back();
        m_freeFences.pop_back();
        return fence;
    }
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    ZODAR_VK_CHECK(vkCreateFence(m_context.device(), &info, nullptr, &fence));
    return fence;
}

UploadTicket UploadQueue::submit(Work&& work) {
    poll();
    Submission s;
    s.value = ++m_nextValue;
    s.cmd = takeCommandBuffer();
    s.fence = takeFence();
    s.ownership = makeOwnershipTransfer(work.images, work.exclusiveBuffers, m_family, m_graphicsFamily);
    s.work = std::move(work);

    ZODAR_VK_CHECK(vkResetCommandBuffer(s.cmd, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ZODAR_VK_CHECK(vkBeginCommandBuffer(s.cmd, &begin));
    for (const auto& record : s.work.commands) {
        record(s.cmd);
    }
    recordBarriers(s.cmd, s.ownership.releaseImages, s.ownership.releaseBuffers);
    ZODAR_VK_CHECK(vkEndCommandBuffer(s.cmd));
    s.work.commands.clear();

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = s.cmd;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = m_timeline;
    signal.value = s.value;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = &cmdInfo;
    info.signalSemaphoreInfoCount = 1;
    info.pSignalSemaphoreInfos = &signal;
    // The fence is for host waits: older synchronization-validation layers do not model
    // host waits on timeline semaphores (see UploadContext::immediate).
    ZODAR_VK_CHECK(vkQueueSubmit2(m_queue, 1, &info, s.fence));

    ++m_stats.submitted;
    m_stats.stagingBytesInFlight += s.work.stagingBytes;
    const UploadTicket ticket{s.value};
    m_submissions.push_back(std::move(s));
    return ticket;
}

void UploadQueue::finishTransfer(Submission& s) {
    const VkDevice device = m_context.device();
    ZODAR_VK_CHECK(vkResetFences(device, 1, &s.fence));
    m_freeFences.push_back(s.fence);
    m_freeCmds.push_back(s.cmd);
    s.fence = VK_NULL_HANDLE;
    s.cmd = VK_NULL_HANDLE;
    s.work.staging.clear();
    if (!s.work.keepAlive.empty()) {
        // May still be referenced by graphics work: free after the next graphics submission.
        m_context.deletionQueue().retire([keep = std::move(s.work.keepAlive)] {});
    }
    m_stats.stagingBytesInFlight -= s.work.stagingBytes;
    m_stats.bytesUploaded += s.work.stagingBytes;
    ++m_stats.completed;
    s.state = State::TransferDone;
}

void UploadQueue::poll() {
    for (Submission& s : m_submissions) {
        if (s.state == State::InFlight && vkGetFenceStatus(m_context.device(), s.fence) == VK_SUCCESS) {
            finishTransfer(s);
        }
    }
    // Blocking acquires whose graphics submission has completed.
    const uint64_t graphicsDone = m_context.timeline().completedValue();
    std::erase_if(m_acquireCmds, [&](const AcquireCmd& a) {
        if (a.graphicsValue > graphicsDone) return false;
        m_freeAcquireCmds.push_back(a.cmd);
        return true;
    });
}

void UploadQueue::prune() {
    while (!m_submissions.empty() && m_submissions.front().state == State::Acquired) {
        m_prunedUpTo = m_submissions.front().value;
        m_submissions.pop_front();
    }
}

UploadQueue::Submission* UploadQueue::find(uint64_t value) {
    for (Submission& s : m_submissions) {
        if (s.value == value) return &s;
    }
    return nullptr;
}

const UploadQueue::Submission* UploadQueue::find(uint64_t value) const {
    return const_cast<UploadQueue*>(this)->find(value);
}

bool UploadQueue::isTransferComplete(UploadTicket ticket) const {
    const Submission* s = find(ticket.value);
    if (!s) return ticket.value <= m_nextValue;
    return s->state != State::InFlight || vkGetFenceStatus(m_context.device(), s->fence) == VK_SUCCESS;
}

bool UploadQueue::isReady(UploadTicket ticket) const {
    if (!ticket.valid()) return true;
    const Submission* s = find(ticket.value);
    return s ? s->state == State::Acquired : ticket.value <= m_nextValue;
}

uint32_t UploadQueue::acquireCompleted(VkCommandBuffer graphicsCmd) {
    poll();
    uint32_t count = 0;
    uint64_t maxValue = 0;
    for (Submission& s : m_submissions) {
        if (s.state != State::TransferDone) continue;
        recordBarriers(graphicsCmd, s.ownership.acquireImages, s.ownership.acquireBuffers);
        s.state = State::Acquired;
        maxValue = std::max(maxValue, s.value);
        ++count;
    }
    if (count > 0) {
        // Already signalled: orders the acquires after the transfer without a stall.
        m_context.addGraphicsWait(m_timeline, maxValue);
    }
    prune();
    return count;
}

void UploadQueue::wait(UploadTicket ticket) {
    Submission* s = find(ticket.value);
    if (!s || s->state == State::Acquired) {
        return;
    }
    if (s->state == State::InFlight) {
        ZODAR_VK_CHECK(vkWaitForFences(m_context.device(), 1, &s->fence, VK_TRUE, UINT64_MAX));
        finishTransfer(*s);
    }
    poll();
    if (s->ownership.needsAcquire()) {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (!m_freeAcquireCmds.empty()) {
            cmd = m_freeAcquireCmds.back();
            m_freeAcquireCmds.pop_back();
        } else {
            VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            info.commandPool = m_acquirePool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;
            ZODAR_VK_CHECK(vkAllocateCommandBuffers(m_context.device(), &info, &cmd));
        }
        ZODAR_VK_CHECK(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ZODAR_VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
        recordBarriers(cmd, s->ownership.acquireImages, s->ownership.acquireBuffers);
        ZODAR_VK_CHECK(vkEndCommandBuffer(cmd));
        VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmdInfo.commandBuffer = cmd;
        GraphicsSubmit submit;
        submit.commandBuffers = std::span(&cmdInfo, 1);
        m_context.addGraphicsWait(m_timeline, s->value);
        const uint64_t value = m_context.submitGraphics(submit);
        m_acquireCmds.push_back({cmd, value});
    } else {
        // Nothing to acquire; later graphics work still waits for the transfer's writes.
        m_context.addGraphicsWait(m_timeline, s->value);
    }
    s->state = State::Acquired;
    prune();
}

void UploadQueue::waitIdle() {
    for (Submission& s : m_submissions) {
        if (s.state == State::InFlight) {
            ZODAR_VK_CHECK(vkWaitForFences(m_context.device(), 1, &s.fence, VK_TRUE, UINT64_MAX));
            finishTransfer(s);
        }
    }
}

UploadQueue::Stats UploadQueue::stats() const {
    Stats s = m_stats;
    s.inFlight = static_cast<uint32_t>(m_stats.submitted - m_stats.completed);
    return s;
}

} // namespace zodar::gfx
