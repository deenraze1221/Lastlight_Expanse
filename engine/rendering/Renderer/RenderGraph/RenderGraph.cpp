#include "Renderer/RenderGraph/RenderGraph.h"

#include "Core/Error.h"
#include "Core/Log.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/GpuProfiler.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <algorithm>

namespace zodar::gfx {

// --- RenderGraphResources ----------------------------------------------------------------

RenderGraphResources::RenderGraphResources(VulkanContext& context, BindlessRegistry& bindless)
    : m_context(context), m_bindless(bindless) {}

RenderGraphResources::~RenderGraphResources() {
    vkDeviceWaitIdle(m_context.device());
    for (auto& p : m_images) {
        if (p->bindlessSlot != UINT32_MAX) {
            m_bindless.releaseTexture(p->bindlessSlot);
        }
    }
}

void RenderGraphResources::beginFrame(uint64_t frameNumber) {
    m_frame = frameNumber;
    for (auto it = m_images.begin(); it != m_images.end();) {
        Physical& p = **it;
        p.inUse = false;
        if (p.lastUsedFrame + kReleaseAfterFrames < frameNumber) {
            // In-flight frames may still use it: release through the deletion queue.
            std::shared_ptr<Physical> retired(it->release());
            BindlessRegistry* bindless = &m_bindless;
            m_context.deletionQueue().retire([retired, bindless] {
                if (retired->bindlessSlot != UINT32_MAX) {
                    bindless->releaseTexture(retired->bindlessSlot);
                }
            });
            it = m_images.erase(it);
        } else {
            ++it;
        }
    }
}

RenderGraphResources::Physical& RenderGraphResources::acquire(const RGImageDesc& desc, VkImageUsageFlags usage,
                                                              const std::string& name) {
    for (auto& p : m_images) {
        const GpuImage& img = p->image;
        if (!p->inUse && img.extent().width == desc.width && img.extent().height == desc.height &&
            img.format() == desc.format && img.mipLevels() == desc.mipLevels && p->usage == usage) {
            p->inUse = true;
            p->lastUsedFrame = m_frame;
            return *p;
        }
    }
    auto p = std::make_unique<Physical>();
    p->image = GpuImage(m_context, {desc.width, desc.height, desc.format, usage, desc.mipLevels, name.c_str()});
    p->usage = usage;
    p->inUse = true;
    p->lastUsedFrame = m_frame;
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        p->bindlessSlot = m_bindless.registerTexture(p->image.view());
    }
    ZODAR_LOG_DEBUG("Render graph: new transient '{}' {}x{}", name, desc.width, desc.height);
    m_images.push_back(std::move(p));
    return *m_images.back();
}

// --- PassBuilder ------------------------------------------------------------------------

void PassBuilder::colorAttachment(RGImage image, LoadOp load, glm::vec4 clear) {
    RenderGraph::Attachment a;
    a.image = image;
    a.load = load;
    a.clear.color = {{clear.r, clear.g, clear.b, clear.a}};
    m_graph.m_passes[m_pass].colors.push_back(a);
    m_graph.addUse(m_pass, image.index, ResourceUsage::ColorAttachment, load != LoadOp::Load);
}

void PassBuilder::depthAttachment(RGImage image, LoadOp load, float clearDepth, bool write) {
    RenderGraph::Attachment a;
    a.image = image;
    a.load = load;
    a.clear.depthStencil = {clearDepth, 0};
    a.write = write;
    m_graph.m_passes[m_pass].depth = a;
    m_graph.addUse(m_pass, image.index, write ? ResourceUsage::DepthAttachment : ResourceUsage::DepthRead,
                   load != LoadOp::Load);
}

void PassBuilder::read(RGImage image, ResourceUsage usage) {
    ZODAR_ENSURE(!isWriteUsage(usage), "read() with a write usage");
    m_graph.addUse(m_pass, image.index, usage, false);
}

void PassBuilder::write(RGImage image, ResourceUsage usage) {
    ZODAR_ENSURE(isWriteUsage(usage), "write() with a read usage");
    m_graph.addUse(m_pass, image.index, usage, false);
}

void PassBuilder::read(RGBuffer buffer, ResourceUsage usage) {
    ZODAR_ENSURE(!isWriteUsage(usage), "read() with a write usage");
    m_graph.addUse(m_pass, buffer.index, usage, false);
}

void PassBuilder::write(RGBuffer buffer, ResourceUsage usage) {
    ZODAR_ENSURE(isWriteUsage(usage), "write() with a read usage");
    m_graph.addUse(m_pass, buffer.index, usage, false);
}

void PassBuilder::sideEffect() {
    m_graph.m_passes[m_pass].sideEffect = true;
}

// --- PassContext ------------------------------------------------------------------------

VkImage PassContext::image(RGImage h) const { return m_graph.m_res.at(h.index).image; }
VkImageView PassContext::view(RGImage h) const { return m_graph.m_res.at(h.index).view; }
VkExtent2D PassContext::extent(RGImage h) const { return m_graph.m_res.at(h.index).extent; }
VkFormat PassContext::format(RGImage h) const { return m_graph.m_res.at(h.index).format; }
VkBuffer PassContext::buffer(RGBuffer h) const { return m_graph.m_res.at(h.index).buffer; }

uint32_t PassContext::bindlessIndex(RGImage h) const {
    const uint32_t slot = m_graph.m_res.at(h.index).bindless;
    ZODAR_ENSURE(slot != UINT32_MAX, "image '" + m_graph.m_res[h.index].name + "' is not sampled through bindless");
    return slot;
}

// --- RenderGraph ------------------------------------------------------------------------

RenderGraph::RenderGraph(RenderGraphResources& resources) : m_resources(resources) {}

RGImage RenderGraph::createImage(std::string name, const RGImageDesc& desc) {
    ZODAR_ENSURE(desc.width > 0 && desc.height > 0 && desc.format != VK_FORMAT_UNDEFINED,
                 "invalid transient image '" + name + "'");
    Resource r;
    r.name = std::move(name);
    r.desc = desc;
    m_res.push_back(std::move(r));
    m_plan.reset();
    return {static_cast<uint32_t>(m_res.size() - 1)};
}

RGImage RenderGraph::importImage(std::string name, const RGImportedImage& image) {
    Resource r;
    r.name = std::move(name);
    r.imported = true;
    r.importedImage = image;
    m_res.push_back(std::move(r));
    m_plan.reset();
    return {static_cast<uint32_t>(m_res.size() - 1)};
}

RGBuffer RenderGraph::importBuffer(std::string name, const RGImportedBuffer& buffer) {
    Resource r;
    r.name = std::move(name);
    r.isImage = false;
    r.imported = true;
    r.importedBuffer = buffer;
    m_res.push_back(std::move(r));
    m_plan.reset();
    return {static_cast<uint32_t>(m_res.size() - 1)};
}

void RenderGraph::addUse(uint32_t pass, uint32_t resource, ResourceUsage usage, bool discard) {
    ZODAR_ENSURE(resource < m_res.size(), "pass '" + m_passes[pass].name + "' uses an invalid resource handle");
    Resource& r = m_res[resource];
    ZODAR_ENSURE(r.isImage || usageState(usage).layout == VK_IMAGE_LAYOUT_UNDEFINED ||
                     usage == ResourceUsage::StorageRead || usage == ResourceUsage::StorageWrite ||
                     usage == ResourceUsage::StorageReadWrite || usage == ResourceUsage::TransferSrc ||
                     usage == ResourceUsage::TransferDst,
                 "buffer '" + r.name + "' used with an image-only usage");
    r.usage |= imageUsageFlags(usage);
    m_passes[pass].uses.push_back({resource, usage, discard});
}

void RenderGraph::addPass(std::string name, PassType type, const SetupFn& setup, ExecuteFn execute) {
    ZODAR_ENSURE(!m_executed, "render graph already executed");
    m_passes.push_back({std::move(name), type, {}, {}, std::nullopt, false, std::move(execute)});
    PassBuilder builder(*this, static_cast<uint32_t>(m_passes.size() - 1));
    setup(builder);
    if (type != PassType::Raster) {
        ZODAR_ENSURE(m_passes.back().colors.empty() && !m_passes.back().depth,
                     "only raster passes may declare attachments");
    }
    m_plan.reset();
}

const GraphPlan& RenderGraph::compile() {
    if (!m_plan) {
        std::vector<PlanResource> resources;
        resources.reserve(m_res.size());
        for (const Resource& r : m_res) {
            PlanResource p;
            p.name = r.name;
            p.isImage = r.isImage;
            p.imported = r.imported;
            const auto& finalState = r.isImage ? r.importedImage.final : r.importedBuffer.final;
            p.initial = r.isImage ? r.importedImage.initial : r.importedBuffer.initial;
            p.hasFinalState = r.imported && finalState.has_value();
            if (p.hasFinalState) {
                p.final = *finalState;
            }
            resources.push_back(p);
        }
        std::vector<PlanPass> passes;
        passes.reserve(m_passes.size());
        for (const Pass& pass : m_passes) {
            passes.push_back({pass.name, pass.uses, pass.sideEffect});
        }
        m_plan = planGraph(resources, passes);
    }
    return *m_plan;
}

size_t RenderGraph::barrierCount() const {
    if (!m_plan) return 0;
    size_t n = m_plan->finalBarriers.size();
    for (const auto& b : m_plan->barriers) n += b.size();
    return n;
}

void RenderGraph::recordBarriers(VkCommandBuffer cmd, const std::vector<PlannedBarrier>& planned) const {
    if (planned.empty()) {
        return;
    }
    std::vector<VkImageMemoryBarrier2> images;
    std::vector<VkBufferMemoryBarrier2> buffers;
    for (const PlannedBarrier& b : planned) {
        const Resource& r = m_res[b.resource];
        if (r.isImage) {
            VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            ib.srcStageMask = b.src.stages;
            ib.srcAccessMask = b.src.access;
            ib.dstStageMask = b.dst.stages;
            ib.dstAccessMask = b.dst.access;
            ib.oldLayout = b.src.layout;
            ib.newLayout = b.dst.layout;
            ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib.image = r.image;
            ib.subresourceRange = r.range;
            images.push_back(ib);
        } else {
            VkBufferMemoryBarrier2 bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            bb.srcStageMask = b.src.stages;
            bb.srcAccessMask = b.src.access;
            bb.dstStageMask = b.dst.stages;
            bb.dstAccessMask = b.dst.access;
            bb.srcQueueFamilyIndex = bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bb.buffer = r.buffer;
            bb.size = VK_WHOLE_SIZE;
            buffers.push_back(bb);
        }
    }
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(images.size());
    dep.pImageMemoryBarriers = images.data();
    dep.bufferMemoryBarrierCount = static_cast<uint32_t>(buffers.size());
    dep.pBufferMemoryBarriers = buffers.data();
    vkCmdPipelineBarrier2(cmd, &dep);
}

void RenderGraph::execute(const FrameInfo& frame) {
    ZODAR_ENSURE(!m_executed, "render graph executed twice");
    m_executed = true;
    const GraphPlan& plan = compile();
    const VkCommandBuffer cmd = frame.commandBuffer;
    const VulkanContext& context = m_resources.context();
    m_resources.beginFrame(frame.frameNumber);

    // Resolve physical resources.
    for (size_t i = 0; i < m_res.size(); ++i) {
        Resource& r = m_res[i];
        if (r.imported) {
            if (r.isImage) {
                r.image = r.importedImage.image;
                r.view = r.importedImage.view;
                r.extent = r.importedImage.extent;
                r.format = r.importedImage.format;
                r.range = r.importedImage.range;
                r.bindless = r.importedImage.bindlessIndex;
            } else {
                r.buffer = r.importedBuffer.buffer;
            }
            continue;
        }
        if (!plan.resourceUsed[i]) {
            continue; // Only referenced by culled passes.
        }
        auto& physical = m_resources.acquire(r.desc, r.usage, r.name);
        r.image = physical.image.handle();
        r.view = physical.image.view();
        r.extent = physical.image.extent();
        r.format = physical.image.format();
        r.range = physical.image.fullRange();
        r.bindless = physical.bindlessSlot;
    }

    const bool labels = context.debugUtilsEnabled();
    for (size_t i = 0; i < plan.passes.size(); ++i) {
        Pass& pass = m_passes[plan.passes[i]];
        if (labels) {
            VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
            label.pLabelName = pass.name.c_str();
            vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
        }
        const uint32_t gpuScope = frame.profiler ? frame.profiler->beginScope(cmd, pass.name.c_str()) : UINT32_MAX;
        recordBarriers(cmd, plan.barriers[i]);

        VkExtent2D area{};
        if (pass.type == PassType::Raster) {
            std::vector<VkRenderingAttachmentInfo> colors;
            for (const Attachment& a : pass.colors) {
                const Resource& r = m_res[a.image.index];
                VkRenderingAttachmentInfo info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                info.imageView = r.view;
                info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                info.loadOp = a.load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                              : a.load == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                       : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                info.clearValue = a.clear;
                colors.push_back(info);
                area = r.extent;
            }
            VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            if (pass.depth) {
                const Resource& r = m_res[pass.depth->image.index];
                depth.imageView = r.view;
                depth.imageLayout =
                    pass.depth->write ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
                depth.loadOp = pass.depth->load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                               : pass.depth->load == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                                  : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depth.storeOp = pass.depth->write ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_NONE;
                depth.clearValue = pass.depth->clear;
                if (area.width == 0) area = r.extent;
            }
            ZODAR_ENSURE(area.width > 0, "raster pass '" + pass.name + "' has no attachments");
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea = {{0, 0}, area};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = static_cast<uint32_t>(colors.size());
            rendering.pColorAttachments = colors.data();
            rendering.pDepthAttachment = pass.depth ? &depth : nullptr;
            vkCmdBeginRendering(cmd, &rendering);
            const VkViewport viewport{0.0f, 0.0f, static_cast<float>(area.width), static_cast<float>(area.height),
                                      0.0f, 1.0f};
            const VkRect2D scissor{{0, 0}, area};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }

        PassContext ctx(*this, frame, area);
        pass.execute(ctx);

        if (pass.type == PassType::Raster) {
            vkCmdEndRendering(cmd);
        }
        if (frame.profiler) {
            frame.profiler->endScope(cmd, gpuScope);
        }
        if (labels) {
            vkCmdEndDebugUtilsLabelEXT(cmd);
        }
    }
    recordBarriers(cmd, plan.finalBarriers);
}

} // namespace zodar::gfx
