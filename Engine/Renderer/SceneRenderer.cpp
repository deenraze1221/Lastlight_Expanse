#include "Renderer/SceneRenderer.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Frustum.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/FrameAllocator.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cstddef>

namespace raven::gfx {
namespace {

// std140 mirror of RavenFrame in raven/scene.glsl.
struct GpuPointLight {
    glm::vec4 positionRange;
    glm::vec4 colorIntensity;
};
struct GpuFrameData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::vec4 cameraPosition;
    glm::vec4 viewport;
    glm::vec4 ambient;
    glm::vec4 sunDirection;
    glm::vec4 sunColor;
    glm::uvec4 lightCounts;
    GpuPointLight pointLights[SceneRenderer::kMaxPointLights];
};

struct GpuInstance {
    glm::mat4 model;
    glm::mat4 normalMatrix;
};

// Mirrors RavenDraw in raven/scene.glsl.
struct PushConstants {
    VkDeviceAddress frameAddress;
    VkDeviceAddress instanceAddress;
    uint32_t materialIndex;
    uint32_t reserved[3];
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
    RAVEN_ENSURE(desc.framesInFlight >= 1, "framesInFlight must be at least 1");
    m_bindless = std::make_unique<BindlessRegistry>(context);
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
    m_materials.reset();
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
    RAVEN_VK_CHECK(vkCreatePipelineLayout(m_context.device(), &info, nullptr, &m_sceneLayout));
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
    RAVEN_LOG_DEBUG("Scene targets {}x{}", extent.width, extent.height);
}

SceneRenderer::PreparedScene SceneRenderer::prepare(const FrameInfo& frame, VkExtent2D extent, const SceneView& view,
                                                   RenderStats& stats) {
    RAVEN_ENSURE(frame.scratch != nullptr, "scene rendering needs a frame scratch allocator");
    RAVEN_ENSURE(extent.width > 0 && extent.height > 0, "render extent must be non-zero");
    PreparedScene prepared;

    // --- Frame constants -----------------------------------------------------------
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const glm::mat4 viewMatrix = view.camera.viewMatrix();
    const glm::mat4 projection = view.camera.projectionMatrix(aspect);
    const glm::mat4 viewProjection = projection * viewMatrix;

    GpuFrameData data{};
    data.view = viewMatrix;
    data.projection = projection;
    data.viewProjection = viewProjection;
    data.cameraPosition = glm::vec4(view.camera.position, view.time);
    data.viewport = glm::vec4(extent.width, extent.height, 1.0f / extent.width, 1.0f / extent.height);
    data.ambient = glm::vec4(view.lighting.ambientColor * view.lighting.ambientIntensity, 0.0f);
    const DirectionalLight& sun = view.lighting.sun;
    const float sunLength = glm::length(sun.direction);
    data.sunDirection = glm::vec4(sunLength > 0.0f ? sun.direction / sunLength : glm::vec3(0, -1, 0),
                                  sun.enabled ? 1.0f : 0.0f);
    data.sunColor = glm::vec4(sun.color * sun.intensity, 0.0f);
    const auto pointCount = static_cast<uint32_t>(std::min<size_t>(view.lighting.pointLights.size(), kMaxPointLights));
    if (view.lighting.pointLights.size() > kMaxPointLights) {
        RAVEN_LOG_WARN("{} point lights submitted; only {} are used", view.lighting.pointLights.size(), kMaxPointLights);
    }
    data.lightCounts = glm::uvec4(pointCount, 0, 0, 0);
    for (uint32_t i = 0; i < pointCount; ++i) {
        const PointLight& p = view.lighting.pointLights[i];
        data.pointLights[i] = {glm::vec4(p.position, p.range), glm::vec4(p.color, p.intensity)};
    }
    prepared.frameAddress = frame.scratch->upload(data).address;

    // --- Visibility and ordering ---------------------------------------------------------
    const Frustum frustum(viewProjection);
    const glm::vec3 forward = view.camera.forward();
    std::vector<DrawEntry> entries;
    entries.reserve(view.draws.size());
    for (const MeshDraw& draw : view.draws) {
        ++stats.submitted;
        RAVEN_ENSURE(draw.mesh != nullptr && draw.submesh < draw.mesh->submeshes().size(), "invalid MeshDraw");
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

    std::vector<GpuInstance> instances(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const glm::mat4& model = entries[i].draw->transform;
        instances[i] = {model, glm::mat4(glm::inverseTranspose(glm::mat3(model)))};
    }
    prepared.instanceAddress = frame.scratch->upload(std::span<const GpuInstance>(instances)).address;
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
    stats.drawCalls = static_cast<uint32_t>(prepared.batches.size());
    for (const DrawBatch& b : prepared.batches) {
        stats.triangles += static_cast<uint64_t>(b.mesh->submeshes()[b.submesh].indexCount / 3) * b.instanceCount;
    }
    return prepared;
}

void SceneRenderer::recordDraws(VkCommandBuffer cmd, const PreparedScene& scene) const {
    const VkDescriptorSet set = m_bindless->set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneLayout, 0, 1, &set, 0, nullptr);
    const VkShaderStageFlags pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const PushConstants addresses{scene.frameAddress, scene.instanceAddress, 0, {0, 0, 0}};
    vkCmdPushConstants(cmd, m_sceneLayout, pushStages, 0, sizeof(addresses), &addresses);

    const GraphicsPipeline* boundPipeline = nullptr;
    const GpuMesh* boundMesh = nullptr;
    uint32_t boundMaterial = UINT32_MAX;
    for (const DrawBatch& b : scene.batches) {
        if (b.pipeline != boundPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b.pipeline->handle());
            boundPipeline = b.pipeline;
        }
        if (b.material != boundMaterial) {
            vkCmdPushConstants(cmd, m_sceneLayout, pushStages, offsetof(PushConstants, materialIndex),
                               sizeof(uint32_t), &b.material);
            boundMaterial = b.material;
        }
        if (b.mesh != boundMesh) {
            const VkBuffer vb = b.mesh->vertexBuffer().handle();
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, b.mesh->indexBuffer().handle(), 0, VK_INDEX_TYPE_UINT32);
            boundMesh = b.mesh;
        }
        const Submesh& sub = b.mesh->submeshes()[b.submesh];
        vkCmdDrawIndexed(cmd, sub.indexCount, b.instanceCount, sub.firstIndex, 0, b.firstInstance);
    }
}

RenderStats SceneRenderer::render(const FrameInfo& frame, VkExtent2D extent, const SceneView& view) {
    ensureTargets(extent);
    RenderStats stats;
    const PreparedScene prepared = prepare(frame, extent, view, stats);

    // Reference path: hand-written barriers (kept for regression comparison with the graph).
    const VkCommandBuffer cmd = frame.commandBuffer;
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

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_color.view();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{view.clearColor.r, view.clearColor.g, view.clearColor.b, view.clearColor.a}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = m_depth.view();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {0.0f, 0}; // Reverse Z: far = 0.

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &rendering);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    recordDraws(cmd, prepared);
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
    if (statsOut) {
        *statsOut = stats;
    }

    const RGImage color = graph.createImage("SceneColor", {extent.width, extent.height, m_desc.colorFormat, 1});
    const RGImage depth = graph.createImage("SceneDepth", {extent.width, extent.height, m_desc.depthFormat, 1});
    graph.addPass(
        "Scene", RenderGraph::PassType::Raster,
        [&](PassBuilder& b) {
            b.colorAttachment(color, LoadOp::Clear, view.clearColor);
            b.depthAttachment(depth, LoadOp::Clear, 0.0f); // Reverse Z: far = 0.
        },
        [this, prepared](PassContext& ctx) { recordDraws(ctx.cmd(), *prepared); });

    graph.addPass(
        "Resolve", RenderGraph::PassType::Raster,
        [&](PassBuilder& b) {
            b.read(color, ResourceUsage::SampledFragment);
            b.colorAttachment(output, LoadOp::DontCare);
        },
        [this, color, outputFormat](PassContext& ctx) { recordResolve(ctx.cmd(), ctx.bindlessIndex(color), outputFormat); });
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
    RAVEN_ENSURE(m_color.isValid(), "resolve() called before render()");
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

} // namespace raven::gfx
