#pragma once

#include "Renderer/BindlessRegistry.h"
#include "Renderer/Camera.h"
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

namespace raven::gfx {

class GpuMesh;
class UploadContext;
class VulkanContext;

struct DirectionalLight {
    glm::vec3 direction{-0.4f, -1.0f, -0.3f}; // Direction the light travels.
    glm::vec3 color{1.0f};
    float intensity = 3.0f;
    bool enabled = true;
};

struct PointLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 10.0f; // Inverse-square: irradiance = intensity / d^2.
    float range = 0.0f;      // Smooth cut-off distance; 0 = unbounded.
};

struct SceneLighting {
    glm::vec3 ambientColor{1.0f};
    float ambientIntensity = 0.03f;
    DirectionalLight sun;
    std::vector<PointLight> pointLights; // Up to kMaxPointLights are used.
};

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
    float time = 0.0f;
    bool frustumCulling = true;
};

struct RenderStats {
    uint32_t submitted = 0;
    uint32_t culled = 0;
    uint32_t drawCalls = 0;
    uint32_t instances = 0;
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
    static constexpr uint32_t kMaxPointLights = 64;

    struct FrameInfo {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t slot = 0;        // Frame-in-flight index whose previous use has completed.
        uint64_t frameNumber = 0; // Monotonic; frames <= frameNumber - framesInFlight are complete.
    };

    SceneRenderer(const VulkanContext& context, UploadContext& upload, const SceneRendererDesc& desc);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    BindlessRegistry& bindless() { return *m_bindless; }
    MaterialSystem& materials() { return *m_materials; }

    // Records the scene pass. Targets are (re)created to match `extent`. Afterwards the
    // colour target is in SHADER_READ_ONLY_OPTIMAL and depth in DEPTH_ATTACHMENT_OPTIMAL.
    RenderStats render(const FrameInfo& frame, VkExtent2D extent, const SceneView& view);

    // Draws the scene colour into `target` (in COLOR_ATTACHMENT_OPTIMAL) with a fullscreen pass.
    void resolve(const FrameInfo& frame, VkImageView target, VkFormat targetFormat, VkExtent2D extent);

    const GpuImage& colorTarget() const { return m_color; }
    const GpuImage& depthTarget() const { return m_depth; }
    uint32_t colorTargetBindlessIndex() const { return m_colorSlot; }

private:
    struct FrameResources {
        GpuBuffer frameData;
        GpuBuffer instances;
        uint32_t instanceCapacity = 0;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct Retired {
        uint64_t frameNumber;
        std::function<void()> destroy;
    };

    void createLayouts();
    void ensureTargets(VkExtent2D extent, uint64_t frameNumber);
    void ensureInstanceCapacity(FrameResources& frame, uint32_t count);
    void writeFrameSet(FrameResources& frame);
    void collectRetired(uint64_t frameNumber);
    const GraphicsPipeline& resolvePipeline(VkFormat format);

    const VulkanContext& m_context;
    UploadContext& m_upload;
    SceneRendererDesc m_desc;

    std::unique_ptr<BindlessRegistry> m_bindless;
    Texture m_white;
    Texture m_flatNormal;
    std::unique_ptr<MaterialSystem> m_materials;

    VkDescriptorSetLayout m_frameLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_framePool = VK_NULL_HANDLE;
    VkPipelineLayout m_sceneLayout = VK_NULL_HANDLE;
    std::vector<FrameResources> m_frames;

    GpuImage m_color;
    GpuImage m_depth;
    uint32_t m_colorSlot = UINT32_MAX;
    uint32_t m_resolveSampler = 0;

    std::unique_ptr<ShaderModule> m_resolveVs;
    std::unique_ptr<ShaderModule> m_resolveFs;
    std::unordered_map<VkFormat, std::unique_ptr<GraphicsPipeline>> m_resolvePipelines;

    std::vector<Retired> m_retired;
};

} // namespace raven::gfx
