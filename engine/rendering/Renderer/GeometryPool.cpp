#include "Renderer/GeometryPool.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <algorithm>
#include <memory>

namespace zodar::gfx {
namespace {

constexpr VkBufferUsageFlags kVertexUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
constexpr VkBufferUsageFlags kIndexUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

} // namespace

GeometryPool::GeometryPool(VulkanContext& context, uint32_t initialVertices, uint32_t initialIndices)
    : m_context(context), m_vertexCapacity(std::max(initialVertices, 1024u)),
      m_indexCapacity(std::max(initialIndices, 1024u)) {
    m_vertices = GpuBuffer(context, {VkDeviceSize(m_vertexCapacity) * sizeof(Vertex), kVertexUsage,
                                     MemoryUsage::GpuOnly, "GeometryPool.vertices", true});
    m_indices = GpuBuffer(context, {VkDeviceSize(m_indexCapacity) * sizeof(uint32_t), kIndexUsage,
                                    MemoryUsage::GpuOnly, "GeometryPool.indices", true});
}

void GeometryPool::grow(UploadBatch& batch, uint32_t minVertices, uint32_t minIndices) {
    uint32_t newVertexCapacity = m_vertexCapacity;
    while (newVertexCapacity < minVertices) newVertexCapacity *= 2;
    uint32_t newIndexCapacity = m_indexCapacity;
    while (newIndexCapacity < minIndices) newIndexCapacity *= 2;

    GpuBuffer vertices(m_context, {VkDeviceSize(newVertexCapacity) * sizeof(Vertex), kVertexUsage,
                                   MemoryUsage::GpuOnly, "GeometryPool.vertices", true});
    GpuBuffer indices(m_context, {VkDeviceSize(newIndexCapacity) * sizeof(uint32_t), kIndexUsage,
                                  MemoryUsage::GpuOnly, "GeometryPool.indices", true});
    const VkBuffer oldV = m_vertices.handle(), oldI = m_indices.handle();
    const VkBuffer newV = vertices.handle(), newI = indices.handle();
    const VkDeviceSize vBytes = VkDeviceSize(m_vertexCount) * sizeof(Vertex);
    const VkDeviceSize iBytes = VkDeviceSize(m_indexCount) * sizeof(uint32_t);
    // Commands recorded earlier in this batch still target the old buffers: order the copy
    // after them, then move the contents. Later uploads go to the new buffers.
    batch.record([=](VkCommandBuffer cmd) {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);
        if (vBytes) {
            const VkBufferCopy region{0, 0, vBytes};
            vkCmdCopyBuffer(cmd, oldV, newV, 1, &region);
        }
        if (iBytes) {
            const VkBufferCopy region{0, 0, iBytes};
            vkCmdCopyBuffer(cmd, oldI, newI, 1, &region);
        }
    });
    // The old buffers may still be referenced by in-flight frames and by this batch.
    auto retiredV = std::make_shared<GpuBuffer>(std::move(m_vertices));
    auto retiredI = std::make_shared<GpuBuffer>(std::move(m_indices));
    // The batch's copy reads them too: freed after the transfer and the graphics queue.
    batch.keepAlive(retiredV);
    batch.keepAlive(retiredI);
    m_vertices = std::move(vertices);
    m_indices = std::move(indices);
    ZODAR_LOG_DEBUG("GeometryPool grown to {} vertices / {} indices", newVertexCapacity, newIndexCapacity);
    m_vertexCapacity = newVertexCapacity;
    m_indexCapacity = newIndexCapacity;
}

GeometryPool::Allocation GeometryPool::upload(UploadBatch& batch, std::span<const Vertex> vertices,
                                              std::span<const uint32_t> indices) {
    ZODAR_ENSURE(!vertices.empty() && !indices.empty(), "GeometryPool::upload with empty data");
    const uint64_t needV = uint64_t(m_vertexCount) + vertices.size();
    const uint64_t needI = uint64_t(m_indexCount) + indices.size();
    ZODAR_ENSURE(needV < (1ull << 31) && needI < (1ull << 31), "GeometryPool exceeds 2^31 vertices or indices");
    if (needV > m_vertexCapacity || needI > m_indexCapacity) {
        grow(batch, static_cast<uint32_t>(needV), static_cast<uint32_t>(needI));
    }
    Allocation a{m_vertexCount, m_indexCount};
    batch.copyToBuffer(vertices.data(), vertices.size_bytes(), m_vertices, VkDeviceSize(a.baseVertex) * sizeof(Vertex));
    batch.copyToBuffer(indices.data(), indices.size_bytes(), m_indices, VkDeviceSize(a.baseIndex) * sizeof(uint32_t));
    m_vertexCount = static_cast<uint32_t>(needV);
    m_indexCount = static_cast<uint32_t>(needI);
    return a;
}

} // namespace zodar::gfx
