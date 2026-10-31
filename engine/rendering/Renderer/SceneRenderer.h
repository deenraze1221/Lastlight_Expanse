#pragma once

#include "Renderer/BindlessRegistry.h"
#include "Renderer/Camera.h"
#include "Renderer/FrameInfo.h"
#include "Renderer/Lighting/ClusteredLighting.h"
#include "Renderer/Lighting/Environment.h"
#include "Renderer/Lighting/Lights.h"
#include "Renderer/Material.h"
#include "Renderer/Resources/GpuBuffer.h"
#include "Renderer/Resources/GpuImage.h"
#include "Renderer/Texture.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace zodar::gfx {

class GeometryPool;
class GpuMesh;
class GpuScene;
class GraphicsPipeline;
class RenderGraph;
class UploadContext;
struct RGImage;
class VulkanContext;

struct MeshDraw {
    const GpuMesh* mesh = nullptr;
    uint32_t submesh = 0;
    MaterialHandle material = 0;
    glm::mat4 transform{1.0f};
};

struct SceneView {
    Camera camera;
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f}; // Linear HDR colour.
    SceneLighting lighting;
    std::span<const MeshDraw> draws;
    // Optional persistent GPU-driven scene (render-graph path only). Its opaque/masked
    // instances are culled and drawn on the GPU; `draws` are still drawn by the CPU path.
    GpuScene* gpuScene = nullptr;
    float time = 0.0f;
    bool frustumCulling = true;
    LightingDebugView debugView = LightingDebugView::None;
    // Reference mode: every light is evaluated for every fragment (clustered culling skipped
    // in shading). Produces the same image as clustered shading; used to validate it.
    bool bruteForceLights = false;
};

struct RenderStats {
    uint32_t submitted = 0;
    uint32_t culled = 0;
    uint32_t drawCalls = 0;
    uint32_t instances = 0;     // Visible instances drawn.
    uint64_t triangles = 0;
    uint32_t commandDraws = 0;  // vkCmdDraw* calls recorded (a multi-draw indirect call counts once).
    // GPU-driven results are read back when the frame slot is reused, so they describe the
    // frame rendered framesInFlight frames earlier (0 when only the CPU path drew).
    uint32_t statsLatencyFrames = 0;
    // Clustered lighting (GPU counters, same latency as above; valid == false until available).
    uint32_t lights = 0;
    uint32_t clusters = 0;
    uint32_t clusterAssignments = 0;   // Sum of cluster light-list lengths
    uint32_t maxLightsInCluster = 0;   // Longest list found (before clamping)
    uint32_t overflowClusters = 0;     // Clusters with more lights than the list capacity
    bool lightingStatsValid = false;
};

struct SceneRendererDesc {
    std::filesystem::path shaderDirectory;
    VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; // HDR scene target.
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    uint32_t framesInFlight = 2;
};

// Forward renderer for meshes with engine materials. Renders into its own HDR colour and
// reverse-Z depth targets, then resolves to an output image. Visual style is entirely
// defined by materials, shaders and lighting supplied by the caller.
class SceneRenderer {
public:
    using FrameInfo = gfx::FrameInfo;

    SceneRenderer(VulkanContext& context, UploadContext& upload, const SceneRendererDesc& desc);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    BindlessRegistry& bindless() { return *m_bindless; }
    MaterialSystem& materials() { return *m_materials; }
    // Shared geometry for meshes that should be drawable by a GpuScene.
    GeometryPool& geometry() { return *m_geometry; }
    ClusteredLighting& clusteredLighting() { return *m_lighting; }
    // Bakes environments for SceneLighting::environment; owns the DFG LUT.
    IblBaker& ibl() { return *m_ibl; }

    void setLightingSettings(const LightingSettings& settings) { m_lightingSettings = settings; }
    const LightingSettings& lightingSettings() const { return m_lightingSettings; }

    // Records the scene pass. Targets are (re)created to match `extent`. Afterwards the
    // colour target is in SHADER_READ_ONLY_OPTIMAL and depth in DEPTH_ATTACHMENT_OPTIMAL.
    RenderStats render(const FrameInfo& frame, VkExtent2D extent, const SceneView& view);

    // Draws the scene colour into `target` (in COLOR_ATTACHMENT_OPTIMAL) with a fullscreen pass.
    // The output is display-referred: sRGB-encoded (by hardware for _SRGB formats, in the
    // shader for UNORM formats).
    void resolve(const FrameInfo& frame, VkImageView target, VkFormat targetFormat, VkExtent2D extent);

    // Render-graph path: adds "Scene" (HDR colour + depth, transient) and "Resolve" (into
    // `output`) passes. Returns the scene colour so later passes can read it. The CPU
    // work (culling, sorting, uploads) happens here; GPU commands are recorded at execution.
    struct GraphOutputs {
        uint32_t color = UINT32_MAX; // RGImage indices.
        uint32_t depth = UINT32_MAX;
    };
    GraphOutputs addToGraph(RenderGraph& graph, const FrameInfo& frame, VkExtent2D extent, const SceneView& view,
                            const RGImage& output, VkFormat outputFormat, RenderStats* stats = nullptr);

    const GpuImage& colorTarget() const { return m_color; }
    const GpuImage& depthTarget() const { return m_depth; }
    uint32_t colorTargetBindlessIndex() const { return m_colorSlot; }

private:

    struct DrawBatch {
        const GraphicsPipeline* pipeline;
        const GpuMesh* mesh;
        uint32_t submesh;
        MaterialHandle material;
        uint32_t firstInstance;
        uint32_t instanceCount;
    };
    struct PreparedScene {
        VkDeviceAddress frameAddress = 0;
        VkDeviceAddress instanceAddress = 0;
        VkDeviceAddress visibleAddress = 0;
        std::vector<DrawBatch> batches;
        glm::mat4 viewProjection{1.0f};
        glm::vec3 cameraPosition{0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        ClusteredLighting::Frame lights;
        size_t firstBlendBatch = 0;
        struct Sky {
            glm::mat4 inverseViewProjection{1.0f};
            uint32_t cube = 0;
            uint32_t sampler = 0;
            float intensity = 1.0f;
            float lod = 0.0f;
        };
        bool drawSky = false;
        bool depthPrepass = false;
        Sky sky;
    };

    // Shared by both paths: culling, sorting, instancing and uploads (CPU only).
    PreparedScene prepare(const FrameInfo& frame, VkExtent2D extent, const SceneView& view, RenderStats& stats);
    enum class DrawSet { Opaque, Blended, Depth }; // Depth: opaque/masked with depth-only pipelines
    void recordDraws(VkCommandBuffer cmd, const PreparedScene& scene, DrawSet set) const;
    void recordSky(VkCommandBuffer cmd, const PreparedScene& scene);
    void recordResolve(VkCommandBuffer cmd, uint32_t sourceIndex, VkFormat targetFormat);
    VkDeviceAddress identityList(uint32_t count);
    void createLayouts();
    void ensureTargets(VkExtent2D extent);
    const GraphicsPipeline& resolvePipeline(VkFormat format);

    VulkanContext& m_context;
    UploadContext& m_upload;
    SceneRendererDesc m_desc;

    std::unique_ptr<BindlessRegistry> m_bindless;
    std::unique_ptr<GeometryPool> m_geometry;
    std::unique_ptr<ClusteredLighting> m_lighting;
    std::unique_ptr<IblBaker> m_ibl;
    std::unique_ptr<ShaderModule> m_skyVs;
    std::unique_ptr<ShaderModule> m_skyFs;
    std::unique_ptr<GraphicsPipeline> m_skyPipeline;
    LightingSettings m_lightingSettings;
    GpuBuffer m_identity; // 0..n-1 instance list for the CPU draw path.
    uint32_t m_identityCount = 0;
    Texture m_white;
    Texture m_flatNormal;
    std::unique_ptr<MaterialSystem> m_materials;

    VkPipelineLayout m_sceneLayout = VK_NULL_HANDLE;

    GpuImage m_color;
    GpuImage m_depth;
    uint32_t m_colorSlot = UINT32_MAX;
    uint32_t m_resolveSampler = 0;

    std::unique_ptr<ShaderModule> m_resolveVs;
    std::unique_ptr<ShaderModule> m_resolveFs;
    std::unordered_map<VkFormat, std::unique_ptr<GraphicsPipeline>> m_resolvePipelines;
};

} // namespace zodar::gfx
