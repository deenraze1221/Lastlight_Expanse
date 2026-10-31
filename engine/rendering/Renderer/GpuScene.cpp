#include "Renderer/GpuScene.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Frustum.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/InstanceData.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

#include <algorithm>
#include <cstring>
#include <numeric>

namespace zodar::gfx {
namespace {

// Mirrors CullData in zodar/cull_common.glsl.
struct GpuCullData {
    glm::vec4 boundsMin;
    glm::vec4 boundsMax;
    glm::uvec4 info; // y: flags
};
static_assert(sizeof(GpuCullData) == 48);

constexpr uint32_t kFlagGpuDrawn = 1u;
constexpr uint32_t kFlagNeverCull = 2u;

constexpr uint32_t kCullGroupSize = 256; // ZODAR_CULL_GROUP_SIZE

// Mirrors CullPush in zodar/cull_common.glsl (shared by the three passes).
struct CullPush {
    VkDeviceAddress cull, order, prefix, blockSums, blockOffsets, groups, commands, visible, stats, planes;
    uint32_t count;
    uint32_t blockCount;
    uint32_t groupCount;
    uint32_t planeCount;
};
static_assert(sizeof(CullPush) == 96);

struct GpuDrawGroup {
    uint32_t base, count;
};

struct GpuCullStats {
    uint32_t visible, drawCommands, triangles, reserved;
};

constexpr VkBufferUsageFlags kStorage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT;

// Same rule as the CPU path: empty bounds are never culled.
GpuCullData cullData(const Bounds& world, bool gpuDrawn) {
    GpuCullData d{};
    if (!world.isEmpty()) {
        d.boundsMin = glm::vec4(world.min, 0.0f);
        d.boundsMax = glm::vec4(world.max, 0.0f);
    }
    d.info = glm::uvec4(0, (gpuDrawn ? kFlagGpuDrawn : 0u) | (world.isEmpty() ? kFlagNeverCull : 0u), 0, 0);
    return d;
}

} // namespace

GpuScene::GpuScene(VulkanContext& context, MaterialSystem& materials, const std::filesystem::path& shaderDirectory,
                   uint32_t framesInFlight)
    : m_context(context), m_materials(materials) {
    const auto compute = [&](const char* file, const char* name) {
        return std::make_unique<ComputePipeline>(context, loadSpirv(shaderDirectory / file),
                                                 std::vector<VkDescriptorSetLayout>{}, sizeof(CullPush), name);
    };
    m_cullScan = compute("cull_scan.comp.spv", "CullScan");
    m_cullBlocks = compute("cull_blocks.comp.spv", "CullBlocks");
    m_cullScatter = compute("cull_scatter.comp.spv", "CullScatter");
    m_statsBuffer = GpuBuffer(context, {sizeof(GpuCullStats), kStorage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        MemoryUsage::GpuOnly, "GpuScene.stats"});
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        m_readback.emplace_back(context, BufferDesc{sizeof(GpuCullStats), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    MemoryUsage::Readback, "GpuScene.readback"});
    }
    m_readbackWritten.assign(framesInFlight, false);
}

GpuScene::~GpuScene() {
    vkDeviceWaitIdle(m_context.device());
}

InstanceHandle GpuScene::add(const GpuMesh& mesh, uint32_t submesh, MaterialHandle material, const glm::mat4& transform) {
    ZODAR_ENSURE(mesh.pool() != nullptr, "GpuScene instances need meshes created in a GeometryPool");
    ZODAR_ENSURE(!m_pool || mesh.pool() == m_pool, "all GpuScene meshes must share one GeometryPool");
    ZODAR_ENSURE(submesh < mesh.submeshes().size(), "GpuScene::add: submesh out of range");
    m_pool = mesh.pool();
    uint32_t slot;
    if (!m_free.empty()) {
        slot = m_free.back();
        m_free.pop_back();
    } else {
        slot = static_cast<uint32_t>(m_instances.size());
        m_instances.emplace_back();
        m_isDirty.push_back(false);
    }
    Instance& inst = m_instances[slot];
    inst.mesh = &mesh;
    inst.submesh = submesh;
    inst.material = material;
    inst.transform = transform;
    inst.world = mesh.submeshes()[submesh].bounds.transformed(transform);
    inst.sequence = m_nextSequence++;
    inst.alive = true;
    inst.blend = m_materials.material(material).alphaMode == AlphaMode::Blend;
    ++m_alive;
    m_structureDirty = true;
    markDirty(slot);
    return slot;
}

void GpuScene::setTransform(InstanceHandle h, const glm::mat4& transform) {
    ZODAR_ENSURE(h < m_instances.size() && m_instances[h].alive, "invalid GpuScene instance");
    Instance& inst = m_instances[h];
    inst.transform = transform;
    inst.world = inst.mesh->submeshes()[inst.submesh].bounds.transformed(transform);
    markDirty(h);
}

void GpuScene::setMaterial(InstanceHandle h, MaterialHandle material) {
    ZODAR_ENSURE(h < m_instances.size() && m_instances[h].alive, "invalid GpuScene instance");
    m_instances[h].material = material;
    m_instances[h].blend = m_materials.material(material).alphaMode == AlphaMode::Blend;
    m_structureDirty = true;
    markDirty(h);
}

void GpuScene::remove(InstanceHandle h) {
    ZODAR_ENSURE(h < m_instances.size() && m_instances[h].alive, "invalid GpuScene instance");
    m_instances[h] = Instance{};
    m_free.push_back(h);
    --m_alive;
    m_structureDirty = true;
    markDirty(h); // Uploads the cleared flags so the slot is skipped.
}

void GpuScene::markDirty(uint32_t slot) {
    if (!m_isDirty[slot]) {
        m_isDirty[slot] = true;
        m_dirty.push_back(slot);
    }
}

VkDeviceSize GpuScene::gpuBytes() const {
    VkDeviceSize total = m_instanceBuffer.size() + m_cullBuffer.size() + m_orderBuffer.size() +
                         m_prefixBuffer.size() + m_blockSums.size() + m_blockOffsets.size() +
                         m_visibleBuffer.size() + m_groupBuffer.size() + m_commandBuffer.size() + m_statsBuffer.size();
    for (const auto& r : m_readback) total += r.size();
    return total;
}

GpuScene::BlendedInstance GpuScene::blended(uint32_t slot) const {
    const Instance& i = m_instances[slot];
    return {slot, i.mesh, i.submesh, i.material, i.world};
}

void GpuScene::rebuildGroups() {
    // Group GPU-drawn instances by (pipeline, material, mesh, submesh), in insertion order
    // within a group: exactly the order the CPU path's stable sort gives submitted draws.
    // The cull keeps this order when it compacts the visible instances.
    struct Key {
        const GraphicsPipeline* pipeline;
        MaterialHandle material;
        const GpuMesh* mesh;
        uint32_t submesh;
        uint64_t sequence;
        uint32_t slot;
    };
    std::vector<Key> keys;
    keys.reserve(m_alive);
    m_blended.clear();
    for (uint32_t s = 0; s < m_instances.size(); ++s) {
        const Instance& i = m_instances[s];
        if (!i.alive) continue;
        if (i.blend) {
            m_blended.push_back(s);
            continue;
        }
        keys.push_back({&m_materials.pipeline(i.material), i.material, i.mesh, i.submesh, i.sequence, s});
    }
    std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
        if (a.pipeline != b.pipeline) return a.pipeline < b.pipeline;
        if (a.material != b.material) return a.material < b.material;
        if (a.mesh != b.mesh) return a.mesh < b.mesh;
        if (a.submesh != b.submesh) return a.submesh < b.submesh;
        return a.sequence < b.sequence;
    });
    // Blended instances are sorted per frame by depth; ties keep insertion order.
    std::sort(m_blended.begin(), m_blended.end(),
              [this](uint32_t a, uint32_t b) { return m_instances[a].sequence < m_instances[b].sequence; });
    m_groups.clear();
    m_buckets.clear();
    m_order.clear();
    m_order.reserve(keys.size());
    for (const Key& k : keys) {
        if (m_groups.empty() || m_groups.back().pipeline != k.pipeline || m_groups.back().material != k.material ||
            m_groups.back().mesh != k.mesh || m_groups.back().submesh != k.submesh) {
            const auto base = static_cast<uint32_t>(m_order.size());
            m_groups.push_back({k.pipeline, k.material, k.mesh, k.submesh, 0, base});
            if (m_buckets.empty() || m_buckets.back().pipeline != k.pipeline) {
                m_buckets.push_back({k.pipeline, static_cast<uint32_t>(m_groups.size() - 1), 0});
            }
            ++m_buckets.back().groupCount;
        }
        ++m_groups.back().count;
        m_order.push_back(k.slot);
    }
    m_orderDirty = true;
    m_structureDirty = false;
}

void GpuScene::ensureCapacity() {
    const auto needed = static_cast<uint32_t>(m_instances.size());
    if (needed > m_capacity || !m_instanceBuffer.isValid()) {
        uint32_t capacity = std::max(m_capacity, 1024u);
        while (capacity < needed) capacity *= 2;
        // Old buffers may be in use by in-flight frames.
        auto oldInstances = std::make_shared<GpuBuffer>(std::move(m_instanceBuffer));
        auto oldCull = std::make_shared<GpuBuffer>(std::move(m_cullBuffer));
        m_context.deletionQueue().retire([oldInstances, oldCull] {});
        m_instanceBuffer = GpuBuffer(m_context, {VkDeviceSize(capacity) * sizeof(GpuInstance), kStorage,
                                                 MemoryUsage::GpuOnly, "GpuScene.instances"});
        m_cullBuffer = GpuBuffer(m_context, {VkDeviceSize(capacity) * sizeof(GpuCullData), kStorage,
                                             MemoryUsage::GpuOnly, "GpuScene.cull"});
        m_capacity = capacity;
        for (uint32_t s = 0; s < m_instances.size(); ++s) markDirty(s); // Full re-upload.
    }
    // Per-position buffers (never empty: they must exist even for an empty scene).
    const auto positions = static_cast<uint32_t>(std::max<size_t>(m_order.size(), 1));
    if (m_orderBuffer.size() < VkDeviceSize(positions) * 4) {
        auto oldOrder = std::make_shared<GpuBuffer>(std::move(m_orderBuffer));
        auto oldPrefix = std::make_shared<GpuBuffer>(std::move(m_prefixBuffer));
        auto oldVisible = std::make_shared<GpuBuffer>(std::move(m_visibleBuffer));
        auto oldSums = std::make_shared<GpuBuffer>(std::move(m_blockSums));
        auto oldOffsets = std::make_shared<GpuBuffer>(std::move(m_blockOffsets));
        m_context.deletionQueue().retire([oldOrder, oldPrefix, oldVisible, oldSums, oldOffsets] {});
        uint32_t capacity = 1024;
        while (capacity < positions) capacity *= 2;
        const uint32_t blocks = (capacity + kCullGroupSize - 1) / kCullGroupSize;
        const auto buffer = [&](VkDeviceSize count, VkBufferUsageFlags extra, const char* name) {
            return GpuBuffer(m_context, {count * 4, kStorage | extra, MemoryUsage::GpuOnly, name});
        };
        m_orderBuffer = buffer(capacity, 0, "GpuScene.order");
        m_prefixBuffer = buffer(capacity, 0, "GpuScene.prefix");
        m_visibleBuffer = buffer(capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "GpuScene.visible");
        m_blockSums = buffer(blocks, 0, "GpuScene.blockSums");
        m_blockOffsets = buffer(blocks + 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "GpuScene.blockOffsets");
        m_orderDirty = true;
    }
    const VkDeviceSize groups = std::max<VkDeviceSize>(m_groups.size(), 1);
    if (m_commandBuffer.size() < groups * sizeof(VkDrawIndexedIndirectCommand)) {
        auto oldGroups = std::make_shared<GpuBuffer>(std::move(m_groupBuffer));
        auto oldCommands = std::make_shared<GpuBuffer>(std::move(m_commandBuffer));
        m_context.deletionQueue().retire([oldGroups, oldCommands] {});
        const VkDeviceSize capacity = std::max<VkDeviceSize>(groups, 64) * 2;
        m_groupBuffer = GpuBuffer(m_context, {capacity * sizeof(GpuDrawGroup), kStorage, MemoryUsage::GpuOnly,
                                              "GpuScene.groups"});
        m_commandBuffer = GpuBuffer(m_context, {capacity * sizeof(VkDrawIndexedIndirectCommand),
                                                kStorage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, MemoryUsage::GpuOnly,
                                                "GpuScene.commands"});
        m_orderDirty = true;
    }
}

GpuScene::FramePasses GpuScene::addCullPasses(RenderGraph& graph, const FrameInfo& frame, const Frustum& frustum) {
    ZODAR_ENSURE(frame.scratch != nullptr, "GpuScene needs a frame scratch allocator");
    FramePasses out;

    // Results of the frame that last used this slot (it has completed).
    if (m_readbackWritten[frame.slot]) {
        GpuBuffer& rb = m_readback[frame.slot];
        rb.invalidate();
        GpuCullStats s;
        std::memcpy(&s, rb.mapped(), sizeof(s));
        out.stats = {s.visible, s.drawCommands, s.triangles, true};
    }

    if (m_structureDirty) rebuildGroups();
    ensureCapacity();

    // Stage dirty instance data in frame scratch memory, coalesced into contiguous ranges.
    std::sort(m_dirty.begin(), m_dirty.end());
    struct Range {
        uint32_t first, count;
        VkDeviceSize instanceOffset, cullOffset;
    };
    std::vector<Range> ranges;
    FrameAllocator::Allocation instanceStage{}, cullStage{};
    if (!m_dirty.empty()) {
        instanceStage = frame.scratch->allocate(VkDeviceSize(m_dirty.size()) * sizeof(GpuInstance), 16);
        cullStage = frame.scratch->allocate(VkDeviceSize(m_dirty.size()) * sizeof(GpuCullData), 16);
        auto* inst = static_cast<GpuInstance*>(instanceStage.data);
        auto* cull = static_cast<GpuCullData*>(cullStage.data);
        for (size_t i = 0; i < m_dirty.size(); ++i) {
            const uint32_t s = m_dirty[i];
            const Instance& src = m_instances[s];
            inst[i] = src.alive ? makeGpuInstance(src.transform, src.material) : GpuInstance{};
            cull[i] = src.alive ? cullData(src.world, !src.blend) : GpuCullData{};
            if (!ranges.empty() && ranges.back().first + ranges.back().count == s) {
                ++ranges.back().count;
            } else {
                ranges.push_back({s, 1, VkDeviceSize(i) * sizeof(GpuInstance), VkDeviceSize(i) * sizeof(GpuCullData)});
            }
            m_isDirty[s] = false;
        }
        frame.scratch->flush(instanceStage); // Written in place above.
        frame.scratch->flush(cullStage);
        m_dirty.clear();
    }
    // Structure (order, groups and the static command fields) only after it changed.
    FrameAllocator::Allocation orderStage{}, groupStage{}, commandStage{};
    if (m_orderDirty && !m_order.empty()) {
        std::vector<GpuDrawGroup> groups(m_groups.size());
        std::vector<VkDrawIndexedIndirectCommand> commands(m_groups.size());
        for (size_t g = 0; g < m_groups.size(); ++g) {
            const Group& group = m_groups[g];
            const Submesh& sub = group.mesh->submeshes()[group.submesh];
            groups[g] = {group.base, group.count};
            commands[g] = {sub.indexCount, 0, group.mesh->baseIndex() + sub.firstIndex,
                           static_cast<int32_t>(group.mesh->baseVertex()), 0};
        }
        orderStage = frame.scratch->upload(std::span<const uint32_t>(m_order), 16);
        groupStage = frame.scratch->upload(std::span<const GpuDrawGroup>(groups), 16);
        commandStage = frame.scratch->upload(std::span<const VkDrawIndexedIndirectCommand>(commands), 16);
    }
    m_orderDirty = false;

    // Planes for the GPU test: exactly the CPU frustum's planes.
    glm::vec4 planes[6]{};
    for (int i = 0; i < frustum.planeCount(); ++i) planes[i] = frustum.planes()[i];
    const FrameAllocator::Allocation planeStage = frame.scratch->upload(planes, 16);

    const auto import = [&](const char* name, const GpuBuffer& buffer, ResourceUsage state) {
        return graph.importBuffer(name, {buffer.handle(), buffer.size(), usageState(state), usageState(state)});
    };
    out.instances = import("GpuScene.instances", m_instanceBuffer, ResourceUsage::UniformRead);
    const RGBuffer cullBuf = import("GpuScene.cull", m_cullBuffer, ResourceUsage::StorageRead);
    const RGBuffer order = import("GpuScene.order", m_orderBuffer, ResourceUsage::StorageRead);
    const RGBuffer groupsBuf = import("GpuScene.groups", m_groupBuffer, ResourceUsage::StorageRead);
    const RGBuffer prefix = import("GpuScene.prefix", m_prefixBuffer, ResourceUsage::StorageRead);
    const RGBuffer blockSums = import("GpuScene.blockSums", m_blockSums, ResourceUsage::StorageRead);
    const RGBuffer blockOffsets = import("GpuScene.blockOffsets", m_blockOffsets, ResourceUsage::StorageRead);
    out.visible = import("GpuScene.visible", m_visibleBuffer, ResourceUsage::UniformRead);
    out.commands = import("GpuScene.commands", m_commandBuffer, ResourceUsage::IndirectArgs);
    out.statsBuffer = import("GpuScene.stats", m_statsBuffer, ResourceUsage::TransferSrc);
    const RGBuffer stats = out.statsBuffer;
    out.instanceAddress = m_instanceBuffer.deviceAddress();
    out.visibleAddress = m_visibleBuffer.deviceAddress();

    const bool uploadInstances = !ranges.empty();
    const bool uploadStructure = orderStage.buffer != VK_NULL_HANDLE;
    graph.addPass(
        "GpuScene.Update", RenderGraph::PassType::Transfer,
        [&](PassBuilder& b) {
            if (uploadInstances) {
                b.write(out.instances, ResourceUsage::TransferDst);
                b.write(cullBuf, ResourceUsage::TransferDst);
            }
            if (uploadStructure) {
                b.write(order, ResourceUsage::TransferDst);
                b.write(groupsBuf, ResourceUsage::TransferDst);
                b.write(out.commands, ResourceUsage::TransferDst);
            }
            b.write(stats, ResourceUsage::TransferDst);
        },
        [=, this, ranges = std::move(ranges)](PassContext& ctx) {
            const VkCommandBuffer cmd = ctx.cmd();
            if (!ranges.empty()) {
                std::vector<VkBufferCopy> instanceCopies, cullCopies;
                for (const Range& r : ranges) {
                    instanceCopies.push_back({instanceStage.offset + r.instanceOffset, VkDeviceSize(r.first) * sizeof(GpuInstance),
                                              VkDeviceSize(r.count) * sizeof(GpuInstance)});
                    cullCopies.push_back({cullStage.offset + r.cullOffset, VkDeviceSize(r.first) * sizeof(GpuCullData),
                                          VkDeviceSize(r.count) * sizeof(GpuCullData)});
                }
                vkCmdCopyBuffer(cmd, instanceStage.buffer, m_instanceBuffer.handle(),
                                static_cast<uint32_t>(instanceCopies.size()), instanceCopies.data());
                vkCmdCopyBuffer(cmd, cullStage.buffer, m_cullBuffer.handle(), static_cast<uint32_t>(cullCopies.size()),
                                cullCopies.data());
            }
            if (uploadStructure) {
                const auto copy = [cmd](const FrameAllocator::Allocation& src, const GpuBuffer& dst) {
                    const VkBufferCopy region{src.offset, 0, src.size};
                    vkCmdCopyBuffer(cmd, src.buffer, dst.handle(), 1, &region);
                };
                copy(orderStage, m_orderBuffer);
                copy(groupStage, m_groupBuffer);
                copy(commandStage, m_commandBuffer);
            }
            vkCmdFillBuffer(cmd, m_statsBuffer.handle(), 0, VK_WHOLE_SIZE, 0);
        });

    const auto count = static_cast<uint32_t>(m_order.size());
    const uint32_t blockCount = (count + kCullGroupSize - 1) / kCullGroupSize;
    const CullPush push{m_cullBuffer.deviceAddress(),   m_orderBuffer.deviceAddress(),
                        m_prefixBuffer.deviceAddress(), m_blockSums.deviceAddress(),
                        m_blockOffsets.deviceAddress(), m_groupBuffer.deviceAddress(),
                        m_commandBuffer.deviceAddress(), m_visibleBuffer.deviceAddress(),
                        m_statsBuffer.deviceAddress(),  planeStage.address,
                        count,
                        blockCount,
                        static_cast<uint32_t>(m_groups.size()),
                        static_cast<uint32_t>(frustum.planeCount())};
    const auto dispatch = [push](PassContext& ctx, const ComputePipeline& pipeline, uint32_t groups) {
        if (push.count == 0) return;
        vkCmdBindPipeline(ctx.cmd(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
        vkCmdPushConstants(ctx.cmd(), pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(ctx.cmd(), groups, 1, 1);
    };
    // 1. Frustum test + workgroup-local exclusive scan.
    graph.addPass(
        "GpuScene.CullScan", RenderGraph::PassType::Compute,
        [&](PassBuilder& b) {
            b.read(cullBuf, ResourceUsage::StorageRead);
            b.read(order, ResourceUsage::StorageRead);
            b.write(prefix, ResourceUsage::StorageWrite);
            b.write(blockSums, ResourceUsage::StorageWrite);
        },
        [this, dispatch, blockCount](PassContext& ctx) { dispatch(ctx, *m_cullScan, blockCount); });
    // 2. Exclusive scan of the block sums (one workgroup).
    graph.addPass(
        "GpuScene.CullBlocks", RenderGraph::PassType::Compute,
        [&](PassBuilder& b) {
            b.read(blockSums, ResourceUsage::StorageRead);
            b.write(blockOffsets, ResourceUsage::StorageWrite);
        },
        [this, dispatch](PassContext& ctx) { dispatch(ctx, *m_cullBlocks, 1); });
    // 3. Scatter visible slots in order; write command instance ranges and statistics.
    const uint32_t scatterGroups =
        (std::max(count, static_cast<uint32_t>(m_groups.size())) + kCullGroupSize - 1) / kCullGroupSize;
    graph.addPass(
        "GpuScene.CullScatter", RenderGraph::PassType::Compute,
        [&](PassBuilder& b) {
            b.read(order, ResourceUsage::StorageRead);
            b.read(prefix, ResourceUsage::StorageRead);
            b.read(blockOffsets, ResourceUsage::StorageRead);
            b.read(groupsBuf, ResourceUsage::StorageRead);
            b.write(out.commands, ResourceUsage::StorageReadWrite);
            b.write(out.visible, ResourceUsage::StorageWrite);
            b.write(stats, ResourceUsage::StorageReadWrite);
        },
        [this, dispatch, scatterGroups](PassContext& ctx) { dispatch(ctx, *m_cullScatter, scatterGroups); });
    return out;
}

void GpuScene::addStatsPass(RenderGraph& graph, const FrameInfo& frame, const FramePasses& passes) {
    GpuBuffer& rb = m_readback[frame.slot];
    const AccessState host{VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    const RGBuffer stats = passes.statsBuffer; // Same graph resource the cull pass wrote.
    const RGBuffer target = graph.importBuffer("GpuScene.readback", {rb.handle(), rb.size(), host, host});
    const VkBuffer src = m_statsBuffer.handle(), dst = rb.handle();
    graph.addPass(
        "GpuScene.Stats", RenderGraph::PassType::Transfer,
        [&](PassBuilder& b) {
            b.read(stats, ResourceUsage::TransferSrc);
            b.write(target, ResourceUsage::TransferDst);
        },
        [src, dst](PassContext& ctx) {
            const VkBufferCopy copy{0, 0, sizeof(GpuCullStats)};
            vkCmdCopyBuffer(ctx.cmd(), src, dst, 1, &copy);
        });
    m_readbackWritten[frame.slot] = true;
}

std::vector<InstanceHandle> GpuScene::readVisibleList(UploadContext& upload) const {
    // Visible count = the block-offset total written by the last cull.
    const auto count = static_cast<uint32_t>(m_order.size());
    if (count == 0) return {};
    const uint32_t blockCount = (count + kCullGroupSize - 1) / kCullGroupSize;
    const std::vector<uint8_t> offsets = upload.readBuffer(m_blockOffsets);
    uint32_t visible = 0;
    std::memcpy(&visible, offsets.data() + VkDeviceSize(blockCount) * 4, 4);
    ZODAR_ENSURE(visible <= count, "GpuScene::readVisibleList: corrupt visible count");
    const std::vector<uint8_t> bytes = upload.readBuffer(m_visibleBuffer);
    std::vector<InstanceHandle> list(visible);
    std::memcpy(list.data(), bytes.data(), VkDeviceSize(visible) * 4);
    return list;
}

uint32_t GpuScene::recordIndirectDraws(VkCommandBuffer cmd, const FramePasses&, bool depthOnly) const {
    if (m_groups.empty() || !m_pool) {
        return 0;
    }
    const VkBuffer vb = m_pool->vertexBuffer();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, m_pool->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    const uint32_t maxCount = std::max(1u, m_context.capabilities().maxDrawIndirectCount);
    uint32_t calls = 0;
    for (const Bucket& bucket : m_buckets) {
        // All groups of a bucket share shader, alpha mode and sidedness, hence the depth pipeline.
        const GraphicsPipeline& pipeline =
            depthOnly ? m_materials.depthPipeline(m_groups[bucket.firstGroup].material) : *bucket.pipeline;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
        for (uint32_t first = 0; first < bucket.groupCount; first += maxCount) {
            const uint32_t count = std::min(maxCount, bucket.groupCount - first);
            vkCmdDrawIndexedIndirect(cmd, m_commandBuffer.handle(),
                                     VkDeviceSize(bucket.firstGroup + first) * sizeof(VkDrawIndexedIndirectCommand), count,
                                     sizeof(VkDrawIndexedIndirectCommand));
            ++calls;
        }
    }
    return calls;
}

} // namespace zodar::gfx
