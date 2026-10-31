#include "Renderer/Lighting/ClusteredLighting.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace zodar::gfx {
namespace {

constexpr uint32_t kLightPoint = 0, kLightSpot = 1;
constexpr uint32_t kGroupSize = 64; // local_size_x of cluster_cull.comp

// Mirrors ClusterParams in cluster_cull.comp.
struct GpuClusterParams {
    glm::mat4 inverseProjection;
    glm::uvec4 grid;
    glm::vec4 depth;
    glm::uvec4 counts;
    glm::vec4 viewport;
};

struct CullPush {
    VkDeviceAddress params, lights, counts, indices, stats;
};

struct GpuClusterStats {
    uint32_t assignments, maxCount, overflow, nonEmpty;
};

constexpr VkBufferUsageFlags kStorage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

constexpr VkPipelineStageFlags2 kGraphicsShaders =
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

} // namespace

const char* toString(LightingDebugView view) {
    switch (view) {
    case LightingDebugView::None: return "none";
    case LightingDebugView::LightCount: return "light-count";
    case LightingDebugView::ClusterSlice: return "cluster-slice";
    case LightingDebugView::Normals: return "normals";
    case LightingDebugView::Direct: return "direct";
    case LightingDebugView::Indirect: return "indirect";
    case LightingDebugView::SpecularIbl: return "specular-ibl";
    }
    return "?";
}

LightingDebugView parseLightingDebugView(const std::string& name) {
    for (uint32_t i = 0; i <= uint32_t(LightingDebugView::SpecularIbl); ++i) {
        if (name == toString(LightingDebugView(i))) return LightingDebugView(i);
    }
    fail("Unknown debug view '" + name + "' (none, light-count, cluster-slice, normals, direct, indirect, specular-ibl)");
}

LightingQuality parseLightingQuality(const std::string& name) {
    for (LightingQuality q : {LightingQuality::Low, LightingQuality::Medium, LightingQuality::High, LightingQuality::Ultra}) {
        if (name == toString(q)) return q;
    }
    fail("Unknown lighting quality '" + name + "' (low, medium, high, ultra)");
}

std::vector<GpuLight> buildGpuLights(const SceneLighting& lighting, const glm::mat4& view, float lightCutoff,
                                     uint32_t maxLights) {
    const size_t total = lighting.pointLights.size() + lighting.spotLights.size();
    const size_t count = std::min<size_t>(total, std::min<uint32_t>(maxLights, 65535u));
    if (total > count) {
        ZODAR_LOG_WARN("{} lights submitted; only {} are used (LightingSettings::maxLights)", total, count);
    }
    std::vector<GpuLight> out;
    out.reserve(count);
    const glm::mat3 viewRotation(view);
    for (const PointLight& p : lighting.pointLights) {
        if (out.size() == count) break;
        GpuLight g{};
        const float range = effectiveLightRange(p.intensity, p.color, p.range, lightCutoff);
        g.positionRange = glm::vec4(p.position, range);
        g.colorType = glm::vec4(p.color * p.intensity, std::bit_cast<float>(kLightPoint));
        g.viewPositionRange = glm::vec4(glm::vec3(view * glm::vec4(p.position, 1.0f)), range);
        out.push_back(g);
    }
    for (const SpotLight& s : lighting.spotLights) {
        if (out.size() == count) break;
        GpuLight g{};
        const float range = effectiveLightRange(s.intensity, s.color, s.range, lightCutoff);
        const float length = glm::length(s.direction);
        const glm::vec3 dir = length > 0.0f ? s.direction / length : glm::vec3(0, -1, 0);
        const glm::vec2 so = spotScaleOffset(s.innerConeRadians, s.outerConeRadians);
        const float outer = std::clamp(s.outerConeRadians, 0.0f, 1.5707f);
        g.positionRange = glm::vec4(s.position, range);
        g.colorType = glm::vec4(s.color * s.intensity, std::bit_cast<float>(kLightSpot));
        g.directionSpot = glm::vec4(dir, so.x);
        g.spotOffsetCone = glm::vec4(so.y, std::cos(outer), std::sin(outer), 0.0f);
        g.viewPositionRange = glm::vec4(glm::vec3(view * glm::vec4(s.position, 1.0f)), range);
        g.viewDirection = glm::vec4(glm::normalize(viewRotation * dir), 0.0f);
        out.push_back(g);
    }
    return out;
}

ClusteredLighting::ClusteredLighting(VulkanContext& context, const std::filesystem::path& shaderDirectory,
                                     uint32_t framesInFlight)
    : m_context(context) {
    m_cull = std::make_unique<ComputePipeline>(context, loadSpirv(shaderDirectory / "cluster_cull.comp.spv"),
                                               std::vector<VkDescriptorSetLayout>{}, sizeof(CullPush),
                                               "ClusterCull");
    m_stats = GpuBuffer(context, {sizeof(GpuClusterStats),
                                  kStorage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  MemoryUsage::GpuOnly, "Lighting.stats"});
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        m_readback.emplace_back(context, BufferDesc{sizeof(GpuClusterStats), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    MemoryUsage::Readback, "Lighting.statsReadback"});
    }
    m_pending.assign(framesInFlight, Stats{});
    ensureCapacity(1, 1, 2);
}

ClusteredLighting::~ClusteredLighting() {
    vkDeviceWaitIdle(m_context.device());
}

void ClusteredLighting::ensureCapacity(uint32_t lights, uint32_t clusters, uint32_t maxPerCluster) {
    if (lights > m_lightCapacity) {
        uint32_t capacity = std::max(m_lightCapacity, 256u);
        while (capacity < lights) capacity *= 2;
        auto old = std::make_shared<GpuBuffer>(std::move(m_lights));
        m_context.deletionQueue().retire([old] {});
        m_lights = GpuBuffer(m_context, {VkDeviceSize(capacity) * sizeof(GpuLight), kStorage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                         MemoryUsage::GpuOnly, "Lighting.lights"});
        m_lightCapacity = capacity;
    }
    const VkDeviceSize indexBytes = VkDeviceSize(clusters) * maxPerCluster * 2; // 16-bit indices
    if (clusters > m_clusterCapacity || indexBytes > m_indexBytes) {
        auto oldCounts = std::make_shared<GpuBuffer>(std::move(m_counts));
        auto oldIndices = std::make_shared<GpuBuffer>(std::move(m_indices));
        m_context.deletionQueue().retire([oldCounts, oldIndices] {});
        m_clusterCapacity = std::max(clusters, m_clusterCapacity);
        m_indexBytes = std::max(indexBytes, m_indexBytes);
        m_counts = GpuBuffer(m_context, {VkDeviceSize(m_clusterCapacity) * 4, kStorage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::GpuOnly,
                                         "Lighting.clusterCounts"});
        m_indices = GpuBuffer(m_context, {m_indexBytes, kStorage, MemoryUsage::GpuOnly, "Lighting.clusterIndices"});
        ZODAR_LOG_DEBUG("Cluster buffers: {} clusters, {:.1f} MiB of light indices", m_clusterCapacity,
                        double(m_indexBytes) / 1048576.0);
    }
}

ClusteredLighting::Frame ClusteredLighting::prepare(const FrameInfo& frame, VkExtent2D extent,
                                                    const SceneLighting& lighting, const LightingSettings& settings,
                                                    const glm::mat4& view, const glm::mat4& projection,
                                                    float nearPlane) {
    ZODAR_ENSURE(frame.scratch != nullptr, "ClusteredLighting needs a frame scratch allocator");
    Frame out;
    out.slot = frame.slot;
    ClusterConfig config = clusterConfig(settings.quality);
    config.maxLightsPerCluster = std::max(2u, config.maxLightsPerCluster & ~1u); // Pairs of 16-bit indices
    out.grid = makeClusterGrid(extent.width, extent.height, config, nearPlane, settings.clusterFar);
    out.maxLightsPerCluster = config.maxLightsPerCluster;

    const std::vector<GpuLight> lights = buildGpuLights(lighting, view, settings.lightCutoff, settings.maxLights);
    out.lightCount = static_cast<uint32_t>(lights.size());
    ensureCapacity(out.lightCount, out.grid.count(), out.maxLightsPerCluster);
    if (!lights.empty()) {
        out.lightStage = frame.scratch->upload(std::span<const GpuLight>(lights), 16);
    }
    GpuClusterParams params{};
    params.inverseProjection = glm::inverse(projection);
    params.grid = glm::uvec4(out.grid.x, out.grid.y, out.grid.z, out.grid.tileSize);
    params.depth = glm::vec4(out.grid.sliceScale, out.grid.sliceBias, out.grid.nearPlane, out.grid.farPlane);
    params.counts = glm::uvec4(out.lightCount, out.maxLightsPerCluster, out.grid.count(), 0);
    params.viewport = glm::vec4(extent.width, extent.height, 1.0f / extent.width, 1.0f / extent.height);
    out.params = frame.scratch->upload(params).address;
    out.lights = m_lights.deviceAddress();
    out.counts = m_counts.deviceAddress();
    out.indices = m_indices.deviceAddress();
    m_pending[frame.slot] = Stats{out.lightCount, out.grid.count(), 0, 0, 0, 0, false};
    return out;
}

void ClusteredLighting::recordUpload(VkCommandBuffer cmd, const Frame& frame) const {
    if (frame.lightCount > 0) {
        const VkBufferCopy copy{frame.lightStage.offset, 0, VkDeviceSize(frame.lightCount) * sizeof(GpuLight)};
        vkCmdCopyBuffer(cmd, frame.lightStage.buffer, m_lights.handle(), 1, &copy);
    }
    vkCmdFillBuffer(cmd, m_stats.handle(), 0, VK_WHOLE_SIZE, 0);
}

void ClusteredLighting::recordCull(VkCommandBuffer cmd, const Frame& frame) const {
    if (frame.lightCount == 0) {
        return; // Shaders skip the cluster lists entirely.
    }
    const CullPush push{frame.params, frame.lights, frame.counts, frame.indices, m_stats.deviceAddress()};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cull->handle());
    vkCmdPushConstants(cmd, m_cull->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (frame.grid.count() + kGroupSize - 1) / kGroupSize, 1, 1);
}

void ClusteredLighting::recordStatsCopy(VkCommandBuffer cmd, const Frame& frame) const {
    const VkBufferCopy copy{0, 0, sizeof(GpuClusterStats)};
    vkCmdCopyBuffer(cmd, m_stats.handle(), m_readback[frame.slot].handle(), 1, &copy);
}

void ClusteredLighting::record(VkCommandBuffer cmd, const Frame& frame) {
    // Previous frames read the lights/lists in shaders (WAR: execution dependency) and wrote the
    // lights, lists, counters and readback (WAW: needs a memory dependency).
    memoryBarrier(cmd, kGraphicsShaders | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT |
                           VK_PIPELINE_STAGE_2_CLEAR_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    recordUpload(cmd, frame);
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | kGraphicsShaders,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    recordCull(cmd, frame);
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  kGraphicsShaders | VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    recordStatsCopy(cmd, frame);
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                  VK_ACCESS_2_HOST_READ_BIT);
}

ClusteredLighting::GraphBuffers ClusteredLighting::addToGraph(RenderGraph& graph, const Frame& frame) {
    const auto import = [&](const char* name, const GpuBuffer& buffer, ResourceUsage state) {
        return graph.importBuffer(name, {buffer.handle(), buffer.size(), usageState(state), usageState(state)});
    };
    GraphBuffers out;
    out.lights = import("Lighting.lights", m_lights, ResourceUsage::UniformRead);
    out.counts = import("Lighting.clusterCounts", m_counts, ResourceUsage::UniformRead);
    out.indices = import("Lighting.clusterIndices", m_indices, ResourceUsage::UniformRead);
    const RGBuffer stats = import("Lighting.stats", m_stats, ResourceUsage::TransferSrc);
    const AccessState host{VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    const GpuBuffer& rb = m_readback[frame.slot];
    const RGBuffer readback = graph.importBuffer("Lighting.statsReadback", {rb.handle(), rb.size(), host, host});

    graph.addPass(
        "Lighting.Upload", RenderGraph::PassType::Transfer,
        [&](PassBuilder& b) {
            b.write(out.lights, ResourceUsage::TransferDst);
            b.write(stats, ResourceUsage::TransferDst);
        },
        [this, frame](PassContext& ctx) { recordUpload(ctx.cmd(), frame); });
    graph.addPass(
        "Lighting.ClusterCull", RenderGraph::PassType::Compute,
        [&](PassBuilder& b) {
            b.read(out.lights, ResourceUsage::StorageRead);
            b.write(out.counts, ResourceUsage::StorageWrite);
            b.write(out.indices, ResourceUsage::StorageWrite);
            b.write(stats, ResourceUsage::StorageReadWrite);
        },
        [this, frame](PassContext& ctx) { recordCull(ctx.cmd(), frame); });
    graph.addPass(
        "Lighting.Stats", RenderGraph::PassType::Transfer,
        [&](PassBuilder& b) {
            b.read(stats, ResourceUsage::TransferSrc);
            b.write(readback, ResourceUsage::TransferDst);
        },
        [this, frame](PassContext& ctx) { recordStatsCopy(ctx.cmd(), frame); });
    return out;
}

ClusteredLighting::Stats ClusteredLighting::readStats(uint32_t slot) {
    Stats s = m_pending[slot];
    GpuBuffer& rb = m_readback[slot];
    rb.invalidate();
    GpuClusterStats g;
    std::memcpy(&g, rb.mapped(), sizeof(g));
    s.assignments = g.assignments;
    s.maxFound = g.maxCount;
    s.overflowClusters = g.overflow;
    s.nonEmptyClusters = g.nonEmpty;
    s.valid = s.clusters > 0;
    return s;
}

VkDeviceSize ClusteredLighting::gpuBytes() const {
    VkDeviceSize total = m_lights.size() + m_counts.size() + m_indices.size() + m_stats.size();
    for (const auto& r : m_readback) total += r.size();
    return total;
}

} // namespace zodar::gfx
