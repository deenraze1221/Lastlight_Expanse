#include "Renderer/Vulkan/GpuTimeline.h"

#include "Renderer/Vulkan/VulkanCheck.h"

namespace zodar::gfx {

GpuTimeline::GpuTimeline(VkDevice device) : m_device(device) {
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = 0;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    info.pNext = &type;
    ZODAR_VK_CHECK(vkCreateSemaphore(device, &info, nullptr, &m_semaphore));
}

GpuTimeline::~GpuTimeline() {
    vkDestroySemaphore(m_device, m_semaphore, nullptr);
}

uint64_t GpuTimeline::completedValue() const {
    uint64_t value = 0;
    ZODAR_VK_CHECK(vkGetSemaphoreCounterValue(m_device, m_semaphore, &value));
    return value;
}

void GpuTimeline::wait(uint64_t value) const {
    VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    info.semaphoreCount = 1;
    info.pSemaphores = &m_semaphore;
    info.pValues = &value;
    ZODAR_VK_CHECK(vkWaitSemaphores(m_device, &info, UINT64_MAX));
}

} // namespace zodar::gfx
