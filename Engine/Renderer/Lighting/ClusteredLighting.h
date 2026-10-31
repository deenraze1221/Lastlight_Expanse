#pragma once

#include "Renderer/FrameInfo.h"
#include "Renderer/Lighting/Lights.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/Resources/GpuBuffer.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace zodar::gfx {

class ComputePipeline;
class VulkanContext;

// GPU mirror of ZodarLight in zodar/clusters.glsl.
struct GpuLight {
    glm::vec4 positionRange;
    glm::vec4 colorType;
    glm::vec4 directionSpot;
    glm::vec4 spotOffsetCone;
    glm::vec4 viewPositionRange;
    glm::vec4 viewDirection;
};
static_assert(sizeof(GpuLight) == 96);

// Converts scene lights to GPU lights (points first, then spots) with effective ranges and
// view-space culling data. Pure; exposed for tests.
std::vector<GpuLight> buildGpuLights(const SceneLighting& lighting, const glm::mat4& view, float lightCutoff,
                                     uint32_t maxLights);

// GPU-driven Clustered Forward+ light assignment.
//
// Per frame: the CPU converts the scene's point and spot lights into GpuLights in frame
// scratch memory (O(lights), no per-cluster CPU work); a transfer copies them into a
// device-local buffer; a compute pass ("Lighting.ClusterCull") builds a fixed-capacity,
// ascending-order light list for every cluster; material shaders read the lists through
// buffer device addresses (zodar/lighting.glsl). Statistics are read back without stalls
// framesInFlight frames later. Works through the render graph or with explicit barriers
// (reference path); both record the same commands.
class ClusteredLighting {
public:
    ClusteredLighting(VulkanContext& context, const std::filesystem::path& shaderDirectory, uint32_t framesInFlight);
    ~ClusteredLighting();

    ClusteredLighting(const ClusteredLighting&) = delete;
    ClusteredLighting& operator=(const ClusteredLighting&) = delete;

    struct Frame {
        ClusterGrid grid;
        uint32_t lightCount = 0;
        uint32_t maxLightsPerCluster = 0;
        FrameAllocator::Allocation lightStage; // Host copy of the lights
        VkDeviceAddress params = 0;            // ClusterParams in frame scratch
        VkDeviceAddress lights = 0;            // Device-local buffers
        VkDeviceAddress counts = 0;
        VkDeviceAddress indices = 0;
        uint32_t slot = 0;
    };

    // CPU work for one frame; (re)allocates cluster buffers for the grid.
    Frame prepare(const FrameInfo& frame, VkExtent2D extent, const SceneLighting& lighting,
                  const LightingSettings& settings, const glm::mat4& view, const glm::mat4& projection,
                  float nearPlane);

    // Reference path: upload + cull + stats copy with explicit barriers. Afterwards the
    // light and cluster buffers are readable by graphics shaders.
    void record(VkCommandBuffer cmd, const Frame& frame);

    // Render-graph path: "Lighting.Upload", "Lighting.ClusterCull", "Lighting.Stats". The
    // returned buffers must be declared as UniformRead by the passes that shade.
    struct GraphBuffers {
        RGBuffer lights, counts, indices;
    };
    GraphBuffers addToGraph(RenderGraph& graph, const Frame& frame);

    struct Stats {
        uint32_t lights = 0;
        uint32_t clusters = 0;
        uint32_t assignments = 0; // Sum of cluster list lengths
        uint32_t maxFound = 0;    // Longest list before clamping
        uint32_t overflowClusters = 0;
        uint32_t nonEmptyClusters = 0;
        bool valid = false;
    };
    // Statistics of the frame that last used `slot` (framesInFlight frames old).
    Stats readStats(uint32_t slot);
    VkDeviceSize gpuBytes() const;
    // Tests/diagnostics: per-cluster light counts of the last executed frame.
    const GpuBuffer& clusterCountsBuffer() const { return m_counts; }

private:
    void ensureCapacity(uint32_t lights, uint32_t clusters, uint32_t maxPerCluster);
    void recordUpload(VkCommandBuffer cmd, const Frame& frame) const;
    void recordCull(VkCommandBuffer cmd, const Frame& frame) const;
    void recordStatsCopy(VkCommandBuffer cmd, const Frame& frame) const;

    VulkanContext& m_context;
    std::unique_ptr<ComputePipeline> m_cull;
    GpuBuffer m_lights;
    GpuBuffer m_counts;
    GpuBuffer m_indices;
    GpuBuffer m_stats;
    std::vector<GpuBuffer> m_readback;
    std::vector<Stats> m_pending; // Per slot: grid info of the frame that wrote the readback
    uint32_t m_lightCapacity = 0;
    uint32_t m_clusterCapacity = 0;
    VkDeviceSize m_indexBytes = 0;
};

} // namespace zodar::gfx
