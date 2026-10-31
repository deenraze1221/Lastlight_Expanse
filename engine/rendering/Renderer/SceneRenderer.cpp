#include "Renderer/SceneRenderer.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Frustum.h"
#include "Renderer/GeometryPool.h"
#include "Renderer/GpuProfiler.h"
#include "Renderer/GpuScene.h"
#include "Renderer/InstanceData.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cstddef>

namespace zodar::gfx {
namespace {

// std430 mirror of ZodarFrameBuffer in zodar/scene.glsl.
struct GpuFrameData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::vec4 cameraPosition;
    glm::vec4 viewport;
    glm::vec4 ambient;
    glm::vec4 sunDirection;
    glm::vec4 sunColor;
    glm::uvec4 lightCounts;   // x: lights, y: debug view, z: flags, w: max lights per cluster
    glm::uvec4 clusterGrid;
    glm::vec4 clusterDepth;
    VkDeviceAddress lightsAddress;
    VkDeviceAddress clusterCountsAddress;
    VkDeviceAddress clusterIndicesAddress;
    VkDeviceAddress reserved0;
    glm::uvec4 ibl;           // Bindless slots: irradiance, prefiltered, DFG LUT, sampler
    glm::vec4 iblParams;      // x: intensity, y: prefiltered mips, z: skybox mip
    glm::vec4 fogColor;       // rgb, w: density (0 = off)
    glm::vec4 fogParams;      // x: height falloff, y: base height, z: start distance, w: max opacity
};
static_assert(sizeof(GpuFrameData) == 416, "GpuFrameData must match zodar/scene.glsl");

constexpr uint32_t kLightingIbl = 1u, kLightingMultiscatter = 2u, kLightingBruteForce = 4u;
constexpr uint32_t kNoSlot = 0xFFFFFFFFu;


// Mirrors ZodarDraw in zodar/scene.glsl.
struct PushConstants {
    VkDeviceAddress frameAddress;
    VkDeviceAddress instanceAddress;
    VkDeviceAddress visibleAddress;
    uint64_t reserved;
};
static_assert(sizeof(PushConstants) == 32);

struct ResolvePush {
    uint32_t sourceTexture;
    uint32_t sourceSampler;
    uint32_t encodeSrgb;
};

// 8/10-bit UNORM display formats without hardware sRGB conversion.
bool needsShaderSrgbEncode(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return true;
    default:
        return false;
    }
}

struct DrawEntry {
    const GraphicsPipeline* pipeline;
    const MeshDraw* draw;
    bool blend;
    float viewDepth; // Distance along the view direction (blend sorting).
};

} // namespace

SceneRenderer::SceneRenderer(VulkanContext& context, UploadContext& upload, const SceneRendererDesc& desc)
    : m_context(context), m_upload(upload), m_desc(desc) {
    ZODAR_ENSURE(desc.framesInFlight >= 1, "framesInFlight must be at least 1");
    m_bindless = std::make_unique<BindlessRegistry>(context);
    m_geometry = std::make_unique<GeometryPool>(context);
    m_lighting = std::make_unique<ClusteredLighting>(context, desc.shaderDirectory, desc.framesInFlight);
    m_ibl = std::make_unique<IblBaker>(context, upload, *m_bindless, desc.shaderDirectory);
    m_skyVs = std::make_unique<ShaderModule>(context.device(), loadSpirv(desc.shaderDirectory / "sky.vert.spv"));
    m_skyFs = std::make_unique<ShaderModule>(context.device(), loadSpirv(desc.shaderDirectory / "sky.frag.spv"));
    {
        UploadBatch batch(upload);
        m_white = Texture(batch, *m_bindless, ImageData::solid(1, 1, 255, 255, 255, 255, ColorSpace::Linear, "DefaultWhite"));
        m_flatNormal = Texture(batch, *m_bindless,
                               ImageData::solid(1, 1, 128, 128, 255, 255, ColorSpace::Linear, "DefaultFlatNormal"));
        batch.submit();
    }
    createLayouts();
    m_materials = std::make_unique<MaterialSystem>(
        context, *m_bindless, m_sceneLayout, MaterialSystem::TargetFormats{desc.colorFormat, desc.depthFormat},
        DefaultTextureSlots{m_white.bindlessIndex(), m_flatNormal.bindlessIndex()}, desc.shaderDirectory);

    m_resolveVs = std::make_unique<ShaderModule>(context.device(), loadSpirv(desc.shaderDirectory / "resolve.vert.spv"));
    m_resolveFs = std::make_unique<ShaderModule>(context.device(), loadSpirv(desc.shaderDirectory / "resolve.frag.spv"));
    SamplerDesc point = SamplerDesc::nearest();
    point.addressU = point.addressV = AddressMode::ClampToEdge;
    point.enableMips = false;
    m_resolveSampler = m_bindless->sampler(point);
}

SceneRenderer::~SceneRenderer() {
    vkDeviceWaitIdle(m_context.device());
    m_context.collectGarbage(); // Old targets retired by this renderer reference m_bindless.
    m_context.deletionQueue().flushAll();
    m_resolvePipelines.clear();
    m_skyPipeline.reset();
    m_materials.reset();
    m_ibl.reset();
    m_lighting.reset();
    m_geometry.reset();
    vkDestroyPipelineLayout(m_context.device(), m_sceneLayout, nullptr);
}

void SceneRenderer::createLayouts() {
    // Set 0: bindless resources. Frame and instance data are passed as buffer addresses.
    const VkDescriptorSetLayout setLayout = m_bindless->layout();
    const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = 1;
    info.pSetLayouts = &setLayout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &push;
    ZODAR_VK_CHECK(vkCreatePipelineLayout(m_context.device(), &info, nullptr, &m_sceneLayout));
}

void SceneRenderer::ensureTargets(VkExtent2D extent) {
    if (m_color.isValid() && m_color.extent().width == extent.width && m_color.extent().height == extent.height) {
        return;
    }
    if (m_color.isValid()) {
        // In-flight frames may still read the old targets and slot: destroy them later.
        auto oldColor = std::make_shared<GpuImage>(std::move(m_color));
        auto oldDepth = std::make_shared<GpuImage>(std::move(m_depth));
        const uint32_t oldSlot = m_colorSlot;
        BindlessRegistry* bindless = m_bindless.get();
        m_context.deletionQueue().retire([oldColor, oldDepth, oldSlot, bindless]() mutable {
            bindless->releaseTexture(oldSlot);
            oldColor.reset();
            oldDepth.reset();
        });
    }
    m_color = GpuImage(m_context, {extent.width, extent.height, m_desc.colorFormat,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   1, "SceneColor"});
    m_depth = GpuImage(m_context, {extent.width, extent.height, m_desc.depthFormat,
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   1, "SceneDepth"});
    m_colorSlot = m_bindless->registerTexture(m_color.view());
    ZODAR_LOG_DEBUG("Scene targets {}x{}", extent.width, extent.height);
}

VkDeviceAddress SceneRenderer::identityList(uint32_t count) {
    const uint32_t needed = std::max(count, 1u);
    if (m_identityCount < needed) {
        uint32_t capacity = std::max(m_identityCount, 4096u);
        while (capacity < needed) capacity *= 2;
        // In-flight frames may still read the old list.
        auto old = std::make_shared<GpuBuffer>(std::move(m_identity));
        m_context.deletionQueue().retire([old] {});
        m_identity = GpuBuffer(m_context, {VkDeviceSize(capacity) * 4,
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                           MemoryUsage::Upload, "IdentityInstanceList"});
        auto* values = static_cast<uint32_t*>(m_identity.mapped());
        for (uint32_t i = 0; i < capacity; ++i) values[i] = i;
        m_identity.flush();
        m_identityCount = capacity;
    }
    return m_identity.deviceAddress();
}

SceneRenderer::PreparedScene SceneRenderer::prepare(const FrameInfo& frame, VkExtent2D extent, const SceneView& view,
                                                   RenderStats& stats) {
    ZODAR_ENSURE(frame.scratch != nullptr, "scene rendering needs a frame scratch allocator");
    ZODAR_ENSURE(extent.width > 0 && extent.height > 0, "render extent must be non-zero");
    PreparedScene prepared;

    // --- Frame constants -----------------------------------------------------------
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const glm::mat4 viewMatrix = view.camera.viewMatrix();
    const glm::mat4 projection = view.camera.projectionMatrix(aspect);
    const glm::mat4 viewProjection = projection * viewMatrix;
    prepared.viewProjection = viewProjection;
    prepared.cameraPosition = view.camera.position;
    prepared.forward = view.camera.forward();

    GpuFrameData data{};
    data.view = viewMatrix;
    data.projection = projection;
    data.viewProjection = viewProjection;
    data.cameraPosition = glm::vec4(view.camera.position, view.time);
    data.viewport = glm::vec4(extent.width, extent.height, 1.0f / extent.width, 1.0f / extent.height);
    data.ambient = glm::vec4(view.lighting.ambientColor * view.lighting.ambientIntensity, 0.0f);
    const FogSettings& fog = view.lighting.fog;
    data.fogColor = glm::vec4(fog.color, fog.enabled ? fog.density : 0.0f);
    data.fogParams = glm::vec4(fog.heightFalloff, fog.baseHeight, fog.startDistance, fog.maxOpacity);
    const DirectionalLight& sun = view.lighting.sun;
    const float sunLength = glm::length(sun.direction);
    data.sunDirection = glm::vec4(sunLength > 0.0f ? sun.direction / sunLength : glm::vec3(0, -1, 0),
                                  sun.enabled ? 1.0f : 0.0f);
    data.sunColor = glm::vec4(sun.color * sun.intensity, 0.0f);

    // Clustered lights: statistics of the frame that last used this slot, then this frame's data.
    const ClusteredLighting::Stats lightStats = m_lighting->readStats(frame.slot);
    if (lightStats.valid) {
        stats.lightingStatsValid = true;
        stats.clusterAssignments = lightStats.assignments;
        stats.maxLightsInCluster = lightStats.maxFound;
        stats.overflowClusters = lightStats.overflowClusters;
    }
    const float nearPlane = view.camera.projection.nearPlane;
    prepared.lights = m_lighting->prepare(frame, extent, view.lighting, m_lightingSettings, viewMatrix, projection,
                                          nearPlane);
    const ClusterGrid& grid = prepared.lights.grid;
    stats.lights = prepared.lights.lightCount;
    stats.clusters = grid.count();
    const uint32_t flags = (m_lightingSettings.multiscatter ? kLightingMultiscatter : 0u) |
                           (view.bruteForceLights ? kLightingBruteForce : 0u);
    data.lightCounts = glm::uvec4(prepared.lights.lightCount, static_cast<uint32_t>(view.debugView), flags,
                                  prepared.lights.maxLightsPerCluster);
    data.clusterGrid = glm::uvec4(grid.x, grid.y, grid.z, grid.tileSize);
    data.clusterDepth = glm::vec4(grid.sliceScale, grid.sliceBias, grid.nearPlane, grid.farPlane);
    data.lightsAddress = prepared.lights.lights;
    data.clusterCountsAddress = prepared.lights.counts;
    data.clusterIndicesAddress = prepared.lights.indices;
    // Image-based lighting (the DFG LUT is always available; it also drives multiscatter).
    const Environment* env = view.lighting.environment;
    data.ibl = glm::uvec4(kNoSlot, kNoSlot, m_ibl->dfgLutSlot(), m_ibl->samplerSlot());
    data.iblParams = glm::vec4(0.0f);
    if (env) {
        data.lightCounts.z |= kLightingIbl;
        data.ibl.x = env->irradianceSlot();
        data.ibl.y = env->prefilteredSlot();
        data.iblParams = glm::vec4(view.lighting.environmentIntensity, float(env->prefilteredMips()), 0.0f, 0.0f);
        if (view.lighting.drawEnvironmentBackground) {
            prepared.drawSky = true;
            prepared.sky.inverseViewProjection = glm::inverse(projection * glm::mat4(glm::mat3(viewMatrix)));
            prepared.sky.cube = env->radianceSlot();
            prepared.sky.sampler = m_ibl->samplerSlot();
            prepared.sky.intensity = view.lighting.environmentIntensity;
        }
    }
    prepared.frameAddress = frame.scratch->upload(data).address;

    // --- Visibility and ordering ---------------------------------------------------------
    const Frustum frustum(viewProjection);
    const glm::vec3 forward = view.camera.forward();
    std::vector<DrawEntry> entries;
    entries.reserve(view.draws.size());
    for (const MeshDraw& draw : view.draws) {
        ++stats.submitted;
        ZODAR_ENSURE(draw.mesh != nullptr && draw.submesh < draw.mesh->submeshes().size(), "invalid MeshDraw");
        const Bounds world = draw.mesh->submeshes()[draw.submesh].bounds.transformed(draw.transform);
        if (view.frustumCulling && !world.isEmpty() && !frustum.intersects(world)) {
            ++stats.culled;
            continue;
        }
        const MaterialDesc& material = m_materials->material(draw.material);
        const bool blend = material.alphaMode == AlphaMode::Blend;
        const glm::vec3 centre = world.isEmpty() ? glm::vec3(draw.transform[3]) : world.center();
        entries.push_back({&m_materials->pipeline(draw.material), &draw, blend,
                           glm::dot(centre - view.camera.position, forward)});
    }
    // Opaque/masked: group by pipeline, material and mesh (enables instancing).
    // Blended: back to front.
    std::stable_sort(entries.begin(), entries.end(), [](const DrawEntry& a, const DrawEntry& b) {
        if (a.blend != b.blend) return !a.blend;
        if (a.blend) return a.viewDepth > b.viewDepth;
        if (a.pipeline != b.pipeline) return a.pipeline < b.pipeline;
        if (a.draw->material != b.draw->material) return a.draw->material < b.draw->material;
        if (a.draw->mesh != b.draw->mesh) return a.draw->mesh < b.draw->mesh;
        return a.draw->submesh < b.draw->submesh;
    });

    // Instance data is written in place into frame memory (no intermediate copy).
    const FrameAllocator::Allocation instanceData =
        frame.scratch->allocate(std::max<VkDeviceSize>(entries.size(), 1) * sizeof(GpuInstance), 16);
    auto* instances = static_cast<GpuInstance*>(instanceData.data);
    for (size_t i = 0; i < entries.size(); ++i) {
        instances[i] = makeGpuInstance(entries[i].draw->transform, entries[i].draw->material);
    }
    frame.scratch->flush(instanceData);
    prepared.instanceAddress = instanceData.address;
    // Draw instance i uses instance i: a persistent 0..n-1 list (grown, never rebuilt per frame).
    prepared.visibleAddress = identityList(static_cast<uint32_t>(entries.size()));
    stats.instances = static_cast<uint32_t>(entries.size());

    for (size_t i = 0; i < entries.size();) {
        const DrawEntry& e = entries[i];
        // Batch consecutive identical opaque draws into one instanced call.
        size_t count = 1;
        while (!e.blend && i + count < entries.size()) {
            const DrawEntry& n = entries[i + count];
            if (n.blend || n.draw->mesh != e.draw->mesh || n.draw->submesh != e.draw->submesh ||
                n.draw->material != e.draw->material) {
                break;
            }
            ++count;
        }
        prepared.batches.push_back({e.pipeline, e.draw->mesh, e.draw->submesh, e.draw->material,
                                    static_cast<uint32_t>(i), static_cast<uint32_t>(count)});
        i += count;
    }
    prepared.depthPrepass = m_lightingSettings.depthPrepass;
    prepared.firstBlendBatch = prepared.batches.size();
    for (size_t b = 0; b < prepared.batches.size(); ++b) {
        if (m_materials->material(prepared.batches[b].material).alphaMode == AlphaMode::Blend) {
            prepared.firstBlendBatch = b;
            break;
        }
    }
    stats.drawCalls = static_cast<uint32_t>(prepared.batches.size());
    stats.commandDraws = stats.drawCalls;
    for (const DrawBatch& b : prepared.batches) {
        stats.triangles += static_cast<uint64_t>(b.mesh->submeshes()[b.submesh].indexCount / 3) * b.instanceCount;
    }
    return prepared;
}

void SceneRenderer::recordDraws(VkCommandBuffer cmd, const PreparedScene& scene, DrawSet drawSet) const {
    const VkDescriptorSet set = m_bindless->set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneLayout, 0, 1, &set, 0, nullptr);
    const VkShaderStageFlags pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const PushConstants addresses{scene.frameAddress, scene.instanceAddress, scene.visibleAddress, 0};
    vkCmdPushConstants(cmd, m_sceneLayout, pushStages, 0, sizeof(addresses), &addresses);

    const GraphicsPipeline* boundPipeline = nullptr;
    VkBuffer boundVertices = VK_NULL_HANDLE;
    const bool blended = drawSet == DrawSet::Blended;
    const size_t begin = blended ? scene.firstBlendBatch : 0;
    const size_t end = blended ? scene.batches.size() : scene.firstBlendBatch;
    for (size_t i = begin; i < end; ++i) {
        const DrawBatch& b = scene.batches[i];
        const GraphicsPipeline* pipeline =
            drawSet == DrawSet::Depth ? &m_materials->depthPipeline(b.material) : b.pipeline;
        if (pipeline != boundPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());
            boundPipeline = pipeline;
        }
        if (b.mesh->vertexBuffer() != boundVertices) { // Pooled meshes share one buffer pair.
            const VkBuffer vb = b.mesh->vertexBuffer();
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, b.mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            boundVertices = vb;
        }
        const Submesh& sub = b.mesh->submeshes()[b.submesh];
        vkCmdDrawIndexed(cmd, sub.indexCount, b.instanceCount, b.mesh->baseIndex() + sub.firstIndex,
                         static_cast<int32_t>(b.mesh->baseVertex()), b.firstInstance);
    }
}

void SceneRenderer::recordSky(VkCommandBuffer cmd, const PreparedScene& scene) {
    if (!scene.drawSky) {
        return;
    }
    if (!m_skyPipeline) {
        GraphicsPipelineDesc desc;
        desc.vertexShader = m_skyVs->handle();
        desc.fragmentShader = m_skyFs->handle();
        desc.colorFormat = m_desc.colorFormat;
        desc.depthFormat = m_desc.depthFormat;
        desc.depthTest = true; // Reverse Z: passes only where the cleared depth (0) remains
        desc.depthWrite = false;
        desc.setLayouts = {m_bindless->layout()};
        desc.pushConstantSize = sizeof(PreparedScene::Sky);
        desc.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
        desc.debugName = "Sky";
        m_skyPipeline = std::make_unique<GraphicsPipeline>(m_context, desc);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline->handle());
    const VkDescriptorSet set = m_bindless->set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline->layout(), 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, m_skyPipeline->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(scene.sky), &scene.sky);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    // Later draws use the shared scene layout again.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneLayout, 0, 1, &set, 0, nullptr);
    const PushConstants addresses{scene.frameAddress, scene.instanceAddress, scene.visibleAddress, 0};
    vkCmdPushConstants(cmd, m_sceneLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(addresses), &addresses);
}

RenderStats SceneRenderer::render(const FrameInfo& frame, VkExtent2D extent, const SceneView& view) {
    ensureTargets(extent);
    RenderStats stats;
    const PreparedScene prepared = prepare(frame, extent, view, stats);

    // Reference path: hand-written barriers (kept for regression comparison with the graph).
    const VkCommandBuffer cmd = frame.commandBuffer;
    m_lighting->record(cmd, prepared.lights);
    // The previous frame may still read colour (resolve) or write depth: order against both.
    transitionImage(cmd, m_color.handle(), m_color.fullRange(), VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    transitionImage(cmd, m_depth.handle(), m_depth.fullRange(), VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    if (prepared.depthPrepass) {
        // Depth prepass: opaque/masked depth only; the shading pass then runs one fragment per pixel.
        VkRenderingAttachmentInfo prepassDepth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        prepassDepth.imageView = m_depth.view();
        prepassDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        prepassDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        prepassDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        prepassDepth.clearValue.depthStencil = {0.0f, 0};
        VkRenderingInfo prepass{VK_STRUCTURE_TYPE_RENDERING_INFO};
        prepass.renderArea = {{0, 0}, extent};
        prepass.layerCount = 1;
        prepass.pDepthAttachment = &prepassDepth;
        vkCmdBeginRendering(cmd, &prepass);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        recordDraws(cmd, prepared, DrawSet::Depth);
        vkCmdEndRendering(cmd);
        transitionImage(cmd, m_depth.handle(), m_depth.fullRange(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_color.view();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{view.clearColor.r, view.clearColor.g, view.clearColor.b, view.clearColor.a}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = m_depth.view();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = prepared.depthPrepass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {0.0f, 0}; // Reverse Z: far = 0.

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &rendering);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    recordDraws(cmd, prepared, DrawSet::Opaque);
    recordSky(cmd, prepared);
    recordDraws(cmd, prepared, DrawSet::Blended);
    vkCmdEndRendering(cmd);

    transitionImage(cmd, m_color.handle(), m_color.fullRange(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    return stats;
}

SceneRenderer::GraphOutputs SceneRenderer::addToGraph(RenderGraph& graph, const FrameInfo& frame, VkExtent2D extent,
                                                      const SceneView& view, const RGImage& output,
                                                      VkFormat outputFormat, RenderStats* statsOut) {
    RenderStats stats;
    auto prepared = std::make_shared<PreparedScene>(prepare(frame, extent, view, stats));

    // --- GPU-driven scene: culled and expanded into indirect draws on the GPU. --------
    GpuScene* gpuScene = view.gpuScene;
    GpuScene::FramePasses gpu;
    struct BlendDraw {
        const GraphicsPipeline* pipeline;
        const GpuMesh* mesh;
        uint32_t submesh;
        uint32_t listIndex;
    };
    auto blendDraws = std::make_shared<std::vector<BlendDraw>>();
    VkDeviceAddress blendListAddress = 0;
    if (gpuScene) {
        ZODAR_ENSURE(gpuScene->geometryPool() == nullptr || gpuScene->geometryPool() == m_geometry.get(),
                     "GpuScene meshes must come from this renderer's geometry pool");
        const Frustum frustum(prepared->viewProjection);
        gpu = gpuScene->addCullPasses(graph, frame, frustum);

        // Blended instances: CPU frustum test and back-to-front sort (same rules as `draws`).
        struct Entry {
            uint32_t slot;
            float depth;
        };
        std::vector<Entry> entries;
        for (uint32_t slot : gpuScene->blendedSlots()) {
            const GpuScene::BlendedInstance b = gpuScene->blended(slot);
            if (view.frustumCulling && !b.world.isEmpty() && !frustum.intersects(b.world)) {
                ++stats.culled;
                continue;
            }
            const glm::vec3 centre = b.world.isEmpty() ? glm::vec3(0.0f) : b.world.center();
            entries.push_back({slot, glm::dot(centre - prepared->cameraPosition, prepared->forward)});
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.depth > b.depth; });
        if (!entries.empty()) {
            std::vector<uint32_t> list(entries.size());
            for (uint32_t i = 0; i < entries.size(); ++i) {
                const GpuScene::BlendedInstance b = gpuScene->blended(entries[i].slot);
                list[i] = entries[i].slot;
                blendDraws->push_back({&m_materials->pipeline(b.material), b.mesh, b.submesh, i});
                stats.triangles += b.mesh->submeshes()[b.submesh].indexCount / 3;
            }
            blendListAddress = frame.scratch->upload(std::span<const uint32_t>(list), 16).address;
        }
        stats.submitted += gpuScene->instanceCount();
        stats.instances += gpu.stats.visible + static_cast<uint32_t>(entries.size());
        stats.drawCalls += gpu.stats.drawCommands + static_cast<uint32_t>(entries.size());
        stats.triangles += gpu.stats.triangles;
        stats.culled = stats.submitted - stats.instances;
        stats.commandDraws += gpuScene->indirectCallCount() + static_cast<uint32_t>(entries.size());
        stats.statsLatencyFrames = gpu.stats.valid ? m_desc.framesInFlight : 0;
    }
    if (statsOut) {
        *statsOut = stats;
    }

    const ClusteredLighting::GraphBuffers lightBuffers = m_lighting->addToGraph(graph, prepared->lights);
    const RGImage color = graph.createImage("SceneColor", {extent.width, extent.height, m_desc.colorFormat, 1});
    const RGImage depth = graph.createImage("SceneDepth", {extent.width, extent.height, m_desc.depthFormat, 1});
    const bool prepass = prepared->depthPrepass;
    if (prepass) {
        graph.addPass(
            "Scene.DepthPrepass", RenderGraph::PassType::Raster,
            [&](PassBuilder& b) {
                b.depthAttachment(depth, LoadOp::Clear, 0.0f);
                if (gpuScene) {
                    b.read(gpu.commands, ResourceUsage::IndirectArgs);
                    b.read(gpu.visible, ResourceUsage::UniformRead);
                    b.read(gpu.instances, ResourceUsage::UniformRead);
                }
            },
            [this, prepared, gpuScene, gpu](PassContext& ctx) {
                const VkCommandBuffer cmd = ctx.cmd();
                if (gpuScene) {
                    const VkDescriptorSet set = m_bindless->set();
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneLayout, 0, 1, &set, 0, nullptr);
                    const PushConstants push{prepared->frameAddress, gpu.instanceAddress, gpu.visibleAddress, 0};
                    vkCmdPushConstants(cmd, m_sceneLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(push), &push);
                    gpuScene->recordIndirectDraws(cmd, gpu, true);
                }
                recordDraws(cmd, *prepared, DrawSet::Depth);
            });
    }
    graph.addPass(
        "Scene", RenderGraph::PassType::Raster,
        [&](PassBuilder& b) {
            b.colorAttachment(color, LoadOp::Clear, view.clearColor);
            b.depthAttachment(depth, prepass ? LoadOp::Load : LoadOp::Clear, 0.0f); // Reverse Z: far = 0.
            b.read(lightBuffers.lights, ResourceUsage::UniformRead);
            b.read(lightBuffers.counts, ResourceUsage::UniformRead);
            b.read(lightBuffers.indices, ResourceUsage::UniformRead);
            if (gpuScene) {
                b.read(gpu.commands, ResourceUsage::IndirectArgs);
                b.read(gpu.visible, ResourceUsage::UniformRead);
                b.read(gpu.instances, ResourceUsage::UniformRead);
            }
        },
        [this, prepared, gpuScene, gpu, blendDraws, blendListAddress, profiler = frame.profiler](PassContext& ctx) {
            const VkCommandBuffer cmd = ctx.cmd();
            // Fragment-shader invocations etc. of the whole scene pass (when supported).
            const uint32_t statsScope = profiler ? profiler->beginStatistics(cmd, "Scene") : UINT32_MAX;
            const VkShaderStageFlags pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            if (gpuScene) {
                // 1. GPU-culled opaque and masked instances.
                const VkDescriptorSet set = m_bindless->set();
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneLayout, 0, 1, &set, 0, nullptr);
                const PushConstants push{prepared->frameAddress, gpu.instanceAddress, gpu.visibleAddress, 0};
                vkCmdPushConstants(cmd, m_sceneLayout, pushStages, 0, sizeof(push), &push);
                gpuScene->recordIndirectDraws(cmd, gpu);
            }
            // 2. CPU-submitted draws (opaque, then blended).
            recordDraws(cmd, *prepared, DrawSet::Opaque);
            recordSky(cmd, *prepared);
            recordDraws(cmd, *prepared, DrawSet::Blended);
            if (gpuScene && !blendDraws->empty()) {
                // 3. Blended GPU-scene instances, back to front.
                const PushConstants push{prepared->frameAddress, gpu.instanceAddress, blendListAddress, 0};
                vkCmdPushConstants(cmd, m_sceneLayout, pushStages, 0, sizeof(push), &push);
                const GraphicsPipeline* bound = nullptr;
                VkBuffer boundVertices = VK_NULL_HANDLE;
                for (const BlendDraw& d : *blendDraws) {
                    if (d.pipeline != bound) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipeline->handle());
                        bound = d.pipeline;
                    }
                    if (d.mesh->vertexBuffer() != boundVertices) {
                        const VkBuffer vb = d.mesh->vertexBuffer();
                        const VkDeviceSize offset = 0;
                        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
                        vkCmdBindIndexBuffer(cmd, d.mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                        boundVertices = vb;
                    }
                    const Submesh& sub = d.mesh->submeshes()[d.submesh];
                    vkCmdDrawIndexed(cmd, sub.indexCount, 1, d.mesh->baseIndex() + sub.firstIndex,
                                     static_cast<int32_t>(d.mesh->baseVertex()), d.listIndex);
                }
            }
            if (profiler) profiler->endStatistics(cmd, statsScope);
        });

    graph.addPass(
        "Resolve", RenderGraph::PassType::Raster,
        [&](PassBuilder& b) {
            b.read(color, ResourceUsage::SampledFragment);
            b.colorAttachment(output, LoadOp::DontCare);
        },
        [this, color, outputFormat](PassContext& ctx) { recordResolve(ctx.cmd(), ctx.bindlessIndex(color), outputFormat); });
    if (gpuScene) {
        gpuScene->addStatsPass(graph, frame, gpu);
    }
    return {color.index, depth.index};
}

const GraphicsPipeline& SceneRenderer::resolvePipeline(VkFormat format) {
    if (auto it = m_resolvePipelines.find(format); it != m_resolvePipelines.end()) {
        return *it->second;
    }
    GraphicsPipelineDesc desc;
    desc.vertexShader = m_resolveVs->handle();
    desc.fragmentShader = m_resolveFs->handle();
    desc.colorFormat = format;
    desc.setLayouts = {m_bindless->layout()};
    desc.pushConstantSize = sizeof(ResolvePush);
    desc.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
    desc.debugName = "Resolve";
    return *m_resolvePipelines.emplace(format, std::make_unique<GraphicsPipeline>(m_context, desc)).first->second;
}

void SceneRenderer::recordResolve(VkCommandBuffer cmd, uint32_t sourceIndex, VkFormat targetFormat) {
    const GraphicsPipeline& pipeline = resolvePipeline(targetFormat);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
    const VkDescriptorSet set = m_bindless->set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 1, &set, 0, nullptr);
    const ResolvePush push{sourceIndex, m_resolveSampler, needsShaderSrgbEncode(targetFormat) ? 1u : 0u};
    vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void SceneRenderer::resolve(const FrameInfo& frame, VkImageView target, VkFormat targetFormat, VkExtent2D extent) {
    ZODAR_ENSURE(m_color.isValid(), "resolve() called before render()");
    const VkCommandBuffer cmd = frame.commandBuffer;
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = target;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    recordResolve(cmd, m_colorSlot, targetFormat);
    vkCmdEndRendering(cmd);
}

} // namespace zodar::gfx
