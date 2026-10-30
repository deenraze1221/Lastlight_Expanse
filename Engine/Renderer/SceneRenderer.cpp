#include "Renderer/SceneRenderer.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/Frustum.h"
#include "Renderer/Mesh.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanCheck.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>

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

struct PushConstants {
    uint32_t materialIndex;
    uint32_t reserved[3];
};

struct ResolvePush {
    uint32_t sourceTexture;
    uint32_t sourceSampler;
};

struct DrawEntry {
    const GraphicsPipeline* pipeline;
    const MeshDraw* draw;
    bool blend;
    float viewDepth; // Distance along the view direction (blend sorting).
};

} // namespace

SceneRenderer::SceneRenderer(const VulkanContext& context, UploadContext& upload, const SceneRendererDesc& desc)
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
    for (auto& r : m_retired) {
        r.destroy();
    }
    m_retired.clear();
    m_resolvePipelines.clear();
    m_materials.reset();
    m_frames.clear();
    vkDestroyPipelineLayout(m_context.device(), m_sceneLayout, nullptr);
    vkDestroyDescriptorPool(m_context.device(), m_framePool, nullptr);
    vkDestroyDescriptorSetLayout(m_context.device(), m_frameLayout, nullptr);
}

void SceneRenderer::createLayouts() {
    const VkDevice device = m_context.device();
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    RAVEN_VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_frameLayout));

    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_desc.framesInFlight},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_desc.framesInFlight}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = m_desc.framesInFlight;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    RAVEN_VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_framePool));

    const VkDescriptorSetLayout setLayouts[2] = {m_bindless->layout(), m_frameLayout};
    const VkPushConstantRange push{stages, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    RAVEN_VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_sceneLayout));

    m_frames.resize(m_desc.framesInFlight);
    for (auto& frame : m_frames) {
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = m_framePool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_frameLayout;
        RAVEN_VK_CHECK(vkAllocateDescriptorSets(device, &alloc, &frame.set));
        frame.frameData = GpuBuffer(m_context, {sizeof(GpuFrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                MemoryUsage::Upload, "FrameData"});
        ensureInstanceCapacity(frame, 1024);
    }
}

void SceneRenderer::ensureInstanceCapacity(FrameResources& frame, uint32_t count) {
    if (count <= frame.instanceCapacity) {
        return;
    }
    uint32_t capacity = std::max(frame.instanceCapacity, 1024u);
    while (capacity < count) {
        capacity *= 2;
    }
    // This slot's previous frame has completed, so the old buffer can be replaced directly.
    frame.instances = GpuBuffer(m_context, {sizeof(GpuInstance) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                            MemoryUsage::Upload, "Instances"});
    frame.instanceCapacity = capacity;
    writeFrameSet(frame);
}

void SceneRenderer::writeFrameSet(FrameResources& frame) {
    const VkDescriptorBufferInfo ubo{frame.frameData.handle(), 0, sizeof(GpuFrameData)};
    const VkDescriptorBufferInfo ssbo{frame.instances.handle(), 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[0].dstSet = frame.set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &ubo;
    writes[1] = writes[0];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &ssbo;
    vkUpdateDescriptorSets(m_context.device(), 2, writes, 0, nullptr);
}

void SceneRenderer::collectRetired(uint64_t frameNumber) {
    // Work retired during frame R was last used by frames <= R; frame R has completed
    // once frameNumber >= R + framesInFlight (its fence has been waited for).
    auto it = std::partition(m_retired.begin(), m_retired.end(), [&](const Retired& r) {
        return r.frameNumber + m_desc.framesInFlight > frameNumber;
    });
    for (auto done = it; done != m_retired.end(); ++done) {
        done->destroy();
    }
    m_retired.erase(it, m_retired.end());
}

void SceneRenderer::ensureTargets(VkExtent2D extent, uint64_t frameNumber) {
    if (m_color.isValid() && m_color.extent().width == extent.width && m_color.extent().height == extent.height) {
        return;
    }
    if (m_color.isValid()) {
        // In-flight frames may still read the old targets and slot: destroy them later.
        auto oldColor = std::make_shared<GpuImage>(std::move(m_color));
        auto oldDepth = std::make_shared<GpuImage>(std::move(m_depth));
        const uint32_t oldSlot = m_colorSlot;
        BindlessRegistry* bindless = m_bindless.get();
        m_retired.push_back({frameNumber, [oldColor, oldDepth, oldSlot, bindless]() mutable {
                                 bindless->releaseTexture(oldSlot);
                                 oldColor.reset();
                                 oldDepth.reset();
                             }});
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

RenderStats SceneRenderer::render(const FrameInfo& frame, VkExtent2D extent, const SceneView& view) {
    RAVEN_ENSURE(frame.slot < m_frames.size(), "frame slot out of range");
    RAVEN_ENSURE(extent.width > 0 && extent.height > 0, "render extent must be non-zero");
    collectRetired(frame.frameNumber);
    ensureTargets(extent, frame.frameNumber);
    FrameResources& resources = m_frames[frame.slot];
    RenderStats stats;

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
    resources.frameData.write(&data, sizeof(data));

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

    ensureInstanceCapacity(resources, static_cast<uint32_t>(std::max<size_t>(entries.size(), 1)));
    auto* instances = static_cast<GpuInstance*>(resources.instances.mapped());
    for (size_t i = 0; i < entries.size(); ++i) {
        const glm::mat4& model = entries[i].draw->transform;
        instances[i] = {model, glm::mat4(glm::inverseTranspose(glm::mat3(model)))};
    }
    if (!entries.empty()) {
        resources.instances.write(instances, sizeof(GpuInstance) * entries.size()); // Flush (no-op when coherent).
    }
    stats.instances = static_cast<uint32_t>(entries.size());

    // --- Recording -------------------------------------------------------------------
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
    const VkDescriptorSet sets[2] = {m_bindless->set(), resources.set};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sceneLayout, 0, 2, sets, 0, nullptr);

    const GraphicsPipeline* boundPipeline = nullptr;
    const GpuMesh* boundMesh = nullptr;
    uint32_t boundMaterial = UINT32_MAX;
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
        if (e.pipeline != boundPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e.pipeline->handle());
            boundPipeline = e.pipeline;
        }
        if (e.draw->material != boundMaterial) {
            const PushConstants push{e.draw->material, {0, 0, 0}};
            vkCmdPushConstants(cmd, m_sceneLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(push), &push);
            boundMaterial = e.draw->material;
        }
        if (e.draw->mesh != boundMesh) {
            const VkBuffer vb = e.draw->mesh->vertexBuffer().handle();
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, e.draw->mesh->indexBuffer().handle(), 0, VK_INDEX_TYPE_UINT32);
            boundMesh = e.draw->mesh;
        }
        const Submesh& sub = e.draw->mesh->submeshes()[e.draw->submesh];
        vkCmdDrawIndexed(cmd, sub.indexCount, static_cast<uint32_t>(count), sub.firstIndex, 0, static_cast<uint32_t>(i));
        ++stats.drawCalls;
        i += count;
    }
    vkCmdEndRendering(cmd);

    transitionImage(cmd, m_color.handle(), m_color.fullRange(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    return stats;
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

void SceneRenderer::resolve(const FrameInfo& frame, VkImageView target, VkFormat targetFormat, VkExtent2D extent) {
    RAVEN_ENSURE(m_color.isValid(), "resolve() called before render()");
    const GraphicsPipeline& pipeline = resolvePipeline(targetFormat);
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
    const VkDescriptorSet set = m_bindless->set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 1, &set, 0, nullptr);
    const ResolvePush push{m_colorSlot, m_resolveSampler};
    vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

} // namespace raven::gfx
