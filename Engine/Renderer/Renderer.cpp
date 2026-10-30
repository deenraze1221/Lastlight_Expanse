#include "Renderer/Renderer.h"

#include "Core/Error.h"
#include "Core/ImageIO.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

#include <vk_mem_alloc.h>

#include <vector>

namespace raven::gfx {
namespace {

VkExtent2D toExtent(FramebufferSize size) {
    return {size.width, size.height};
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

struct Renderer::PendingCapture {
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    std::filesystem::path path;

    ~PendingCapture() {
        if (buffer) {
            vmaDestroyBuffer(allocator, buffer, allocation);
        }
    }
};

Renderer::Renderer(Window& window, const RendererDesc& desc) : m_window(window) {
    m_context = std::make_unique<VulkanContext>(ContextDesc{desc.applicationName, desc.enableValidation}, &window);
    m_swapchain = std::make_unique<VulkanSwapchain>(*m_context, toExtent(window.framebufferSize()), desc.presentMode);
    try {
        createFrameResources();
    } catch (...) {
        destroyFrameResources();
        throw;
    }
    RAVEN_LOG_INFO("Renderer ready ({} frames in flight)", kFramesInFlight);
}

Renderer::~Renderer() {
    if (m_context) {
        vkDeviceWaitIdle(m_context->device());
    }
    m_capture.reset();
    destroyFrameResources();
    m_swapchain.reset();
    m_context.reset();
}

void Renderer::createFrameResources() {
    const VkDevice device = m_context->device();
    for (auto& frame : m_frames) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_context->graphicsQueueFamily();
        RAVEN_VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool));

        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        RAVEN_VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer));

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        RAVEN_VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable));

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        RAVEN_VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight));
    }
}

void Renderer::destroyFrameResources() {
    if (!m_context) {
        return;
    }
    const VkDevice device = m_context->device();
    for (auto& frame : m_frames) {
        if (frame.inFlight) vkDestroyFence(device, frame.inFlight, nullptr);
        if (frame.imageAvailable) vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        if (frame.commandPool) vkDestroyCommandPool(device, frame.commandPool, nullptr); // Frees the buffer.
        frame = {};
    }
}

void Renderer::recreateSwapchain() {
    const FramebufferSize size = m_window.framebufferSize();
    if (size.width == 0 || size.height == 0) {
        return; // Keep the flag set; retry once the window has a drawable area again.
    }
    m_context->waitIdle();
    m_swapchain->recreate(toExtent(size));
    m_swapchainDirty = false;
}

std::optional<FrameContext> Renderer::beginFrame() {
    if (m_window.consumeResized()) {
        m_swapchainDirty = true;
    }
    const FramebufferSize size = m_window.framebufferSize();
    if (m_window.isMinimized() || size.width == 0 || size.height == 0) {
        return std::nullopt;
    }
    if (m_swapchainDirty) {
        recreateSwapchain();
    }

    const VkDevice device = m_context->device();
    FrameResources& frame = m_frames[m_frameSlot];
    RAVEN_VK_CHECK(vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(device, m_swapchain->handle(), UINT64_MAX,
                                                   frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        m_swapchainDirty = true;
        return std::nullopt;
    }
    if (acquire == VK_SUBOPTIMAL_KHR) {
        m_swapchainDirty = true; // Still usable this frame; rebuild before the next one.
    } else if (acquire != VK_SUCCESS) {
        throwVkError(acquire, "vkAcquireNextImageKHR");
    }

    // Reset only after a successful acquire so an early return never leaves the fence unsignalled.
    RAVEN_VK_CHECK(vkResetFences(device, 1, &frame.inFlight));
    RAVEN_VK_CHECK(vkResetCommandPool(device, frame.commandPool, 0));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    RAVEN_VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &begin));

    FrameContext ctx;
    ctx.commandBuffer = frame.commandBuffer;
    ctx.image = m_swapchain->image(imageIndex);
    ctx.imageView = m_swapchain->imageView(imageIndex);
    ctx.format = m_swapchain->format();
    ctx.extent = m_swapchain->extent();
    ctx.imageIndex = imageIndex;
    ctx.frameSlot = m_frameSlot;
    ctx.frameNumber = m_frameNumber;

    // The previous contents are discarded; the acquire semaphore wait (at the
    // colour-output stage) is chained through the barrier's source stage.
    imageBarrier(ctx.commandBuffer, ctx.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    return ctx;
}

void Renderer::endFrame(const FrameContext& ctx) {
    FrameResources& frame = m_frames[ctx.frameSlot];
    const VkCommandBuffer cmd = ctx.commandBuffer;

    if (m_screenshotRequest) {
        recordCapture(ctx);
    } else {
        imageBarrier(cmd, ctx.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    }
    RAVEN_VK_CHECK(vkEndCommandBuffer(cmd));

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = frame.imageAvailable;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = m_swapchain->renderFinished(ctx.imageIndex);
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    RAVEN_VK_CHECK(vkQueueSubmit2(m_context->graphicsQueue(), 1, &submit, frame.inFlight));

    const VkSemaphore renderFinished = signal.semaphore;
    const VkSwapchainKHR swapchain = m_swapchain->handle();
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &ctx.imageIndex;
    const VkResult result = vkQueuePresentKHR(m_context->presentQueue(), &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_swapchainDirty = true;
    } else if (result != VK_SUCCESS) {
        throwVkError(result, "vkQueuePresentKHR");
    }

    if (m_capture) {
        finishCapture(ctx);
    }
    m_frameSlot = (m_frameSlot + 1) % kFramesInFlight;
    ++m_frameNumber;
}

void Renderer::requestScreenshot(std::filesystem::path path) {
    m_screenshotRequest = std::move(path);
}

void Renderer::waitIdle() const {
    m_context->waitIdle();
}

void Renderer::recordCapture(const FrameContext& ctx) {
    const VkCommandBuffer cmd = ctx.commandBuffer;
    auto request = std::move(*m_screenshotRequest);
    m_screenshotRequest.reset();

    if (!m_swapchain->supportsReadback()) {
        RAVEN_LOG_ERROR("Screenshot '{}' skipped: swapchain images do not support transfer reads", request.string());
        m_lastScreenshotOk = false;
        imageBarrier(cmd, ctx.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
        return;
    }

    auto capture = std::make_unique<PendingCapture>();
    capture->allocator = m_context->allocator();
    capture->path = std::move(request);

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = static_cast<VkDeviceSize>(ctx.extent.width) * ctx.extent.height * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    RAVEN_VK_CHECK(vmaCreateBuffer(capture->allocator, &bufferInfo, &allocInfo, &capture->buffer,
                                   &capture->allocation, nullptr));

    imageBarrier(cmd, ctx.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {ctx.extent.width, ctx.extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, ctx.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture->buffer, 1, &region);

    // Make the copy visible to the host and hand the image to presentation.
    VkBufferMemoryBarrier2 hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = capture->buffer;
    hostBarrier.size = VK_WHOLE_SIZE;

    VkImageMemoryBarrier2 presentBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    presentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    presentBarrier.srcAccessMask = VK_ACCESS_2_NONE;
    presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    presentBarrier.dstAccessMask = VK_ACCESS_2_NONE;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.image = ctx.image;
    presentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &hostBarrier;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &presentBarrier;
    vkCmdPipelineBarrier2(cmd, &dependency);

    m_capture = std::move(capture);
}

void Renderer::finishCapture(const FrameContext& ctx) {
    auto capture = std::move(m_capture);
    const VkFence fence = m_frames[ctx.frameSlot].inFlight;
    RAVEN_VK_CHECK(vkWaitForFences(m_context->device(), 1, &fence, VK_TRUE, UINT64_MAX));
    RAVEN_VK_CHECK(vmaInvalidateAllocation(capture->allocator, capture->allocation, 0, VK_WHOLE_SIZE));

    VmaAllocationInfo info;
    vmaGetAllocationInfo(capture->allocator, capture->allocation, &info);
    const auto* src = static_cast<const uint8_t*>(info.pMappedData);
    const size_t pixelCount = static_cast<size_t>(ctx.extent.width) * ctx.extent.height;

    std::vector<uint8_t> rgba(src, src + pixelCount * 4);
    const bool bgra = ctx.format == VK_FORMAT_B8G8R8A8_SRGB || ctx.format == VK_FORMAT_B8G8R8A8_UNORM;
    if (bgra) {
        for (size_t i = 0; i < pixelCount; ++i) {
            std::swap(rgba[i * 4 + 0], rgba[i * 4 + 2]);
        }
    }

    m_lastScreenshotOk = writePpm(capture->path, ctx.extent.width, ctx.extent.height, rgba);
    if (m_lastScreenshotOk) {
        RAVEN_LOG_INFO("Screenshot saved: {} ({}x{}, frame {})", capture->path.string(), ctx.extent.width,
                       ctx.extent.height, ctx.frameNumber);
    } else {
        RAVEN_LOG_ERROR("Failed to write screenshot '{}'", capture->path.string());
    }
}

} // namespace raven::gfx
