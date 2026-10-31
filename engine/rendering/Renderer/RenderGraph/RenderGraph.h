#pragma once

#include "Renderer/FrameInfo.h"
#include "Renderer/RenderGraph/GraphPlanner.h"
#include "Renderer/Resources/GpuImage.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zodar::gfx {

class BindlessRegistry;
class VulkanContext;

struct RGImage {
    uint32_t index = UINT32_MAX;
    bool isValid() const { return index != UINT32_MAX; }
};
struct RGBuffer {
    uint32_t index = UINT32_MAX;
    bool isValid() const { return index != UINT32_MAX; }
};

struct RGImageDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t mipLevels = 1;
};

// An image owned outside the graph (swapchain image, persistent history buffer, ...).
struct RGImportedImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    AccessState initial;                // State on entry.
    std::optional<AccessState> final;   // State required after the graph (none = leave as is).
    uint32_t bindlessIndex = UINT32_MAX; // If it should be sampled through the bindless table.
};

struct RGImportedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    AccessState initial;
    std::optional<AccessState> final;
};

// Persistent physical memory behind transient graph images. Images are pooled by
// (size, format, usage, mips) and reused every frame; images unused for a few frames
// are released through the deletion queue. Sampled transients get a bindless slot.
class RenderGraphResources {
public:
    RenderGraphResources(VulkanContext& context, BindlessRegistry& bindless);
    ~RenderGraphResources();

    RenderGraphResources(const RenderGraphResources&) = delete;
    RenderGraphResources& operator=(const RenderGraphResources&) = delete;

    struct Physical {
        GpuImage image;
        VkImageUsageFlags usage = 0;
        uint32_t bindlessSlot = UINT32_MAX;
        uint64_t lastUsedFrame = 0;
        bool inUse = false;
    };

    void beginFrame(uint64_t frameNumber);
    Physical& acquire(const RGImageDesc& desc, VkImageUsageFlags usage, const std::string& name);
    size_t imageCount() const { return m_images.size(); }
    VulkanContext& context() { return m_context; }
    BindlessRegistry& bindless() { return m_bindless; }

private:
    static constexpr uint64_t kReleaseAfterFrames = 8;
    VulkanContext& m_context;
    BindlessRegistry& m_bindless;
    std::vector<std::unique_ptr<Physical>> m_images;
    uint64_t m_frame = 0;
};

class RenderGraph;

enum class LoadOp { Load, Clear, DontCare };

// Declares a pass's resource usage during addPass().
class PassBuilder {
public:
    void colorAttachment(RGImage image, LoadOp load = LoadOp::Load, glm::vec4 clear = glm::vec4(0.0f));
    // Depth test + write (or test only when `write` is false).
    void depthAttachment(RGImage image, LoadOp load = LoadOp::Load, float clearDepth = 0.0f, bool write = true);
    void read(RGImage image, ResourceUsage usage);
    void write(RGImage image, ResourceUsage usage);
    void read(RGBuffer buffer, ResourceUsage usage);
    void write(RGBuffer buffer, ResourceUsage usage);
    void sideEffect();

private:
    friend class RenderGraph;
    PassBuilder(RenderGraph& graph, uint32_t pass) : m_graph(graph), m_pass(pass) {}
    RenderGraph& m_graph;
    uint32_t m_pass;
};

// Resolved resources while a pass records.
class PassContext {
public:
    VkCommandBuffer cmd() const { return m_frame.commandBuffer; }
    const FrameInfo& frame() const { return m_frame; }
    VkImage image(RGImage h) const;
    VkImageView view(RGImage h) const;
    VkExtent2D extent(RGImage h) const;
    VkFormat format(RGImage h) const;
    uint32_t bindlessIndex(RGImage h) const; // Fails if the image is not sampled.
    VkBuffer buffer(RGBuffer h) const;
    VkExtent2D renderArea() const { return m_renderArea; }

private:
    friend class RenderGraph;
    PassContext(const RenderGraph& graph, const FrameInfo& frame, VkExtent2D area)
        : m_graph(graph), m_frame(frame), m_renderArea(area) {}
    const RenderGraph& m_graph;
    const FrameInfo& m_frame;
    VkExtent2D m_renderArea;
};

// Frame render graph (v1): passes are declared, compiled (culling + barrier planning) and
// executed in declaration order on one command buffer. Raster passes get dynamic
// rendering begin/end with viewport and scissor covering their attachments.
// Not yet: transient memory aliasing, pass reordering, multiple queues.
class RenderGraph {
public:
    enum class PassType { Raster, Compute, Transfer };
    using SetupFn = std::function<void(PassBuilder&)>;
    using ExecuteFn = std::function<void(PassContext&)>;

    explicit RenderGraph(RenderGraphResources& resources);

    RGImage createImage(std::string name, const RGImageDesc& desc);
    RGImage importImage(std::string name, const RGImportedImage& image);
    RGBuffer importBuffer(std::string name, const RGImportedBuffer& buffer);

    void addPass(std::string name, PassType type, const SetupFn& setup, ExecuteFn execute);

    // Plans the graph. Called by execute() if needed; exposed for inspection and tests.
    const GraphPlan& compile();
    void execute(const FrameInfo& frame);

    size_t passCount() const { return m_passes.size(); }
    const std::string& passName(uint32_t pass) const { return m_passes[pass].name; }
    size_t barrierCount() const;

private:
    friend class PassBuilder;
    friend class PassContext;

    struct Attachment {
        RGImage image;
        LoadOp load = LoadOp::Load;
        VkClearValue clear{};
        bool write = true;
    };
    struct Pass {
        std::string name;
        PassType type;
        std::vector<PlanUse> uses;
        std::vector<Attachment> colors;
        std::optional<Attachment> depth;
        bool sideEffect = false;
        ExecuteFn execute;
    };
    struct Resource {
        std::string name;
        bool isImage = true;
        bool imported = false;
        RGImageDesc desc;
        VkImageUsageFlags usage = 0;
        RGImportedImage importedImage;
        RGImportedBuffer importedBuffer;
        // Resolved at execution.
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkExtent2D extent{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageSubresourceRange range{};
        uint32_t bindless = UINT32_MAX;
        VkBuffer buffer = VK_NULL_HANDLE;
    };

    void addUse(uint32_t pass, uint32_t resource, ResourceUsage usage, bool discard);
    void recordBarriers(VkCommandBuffer cmd, const std::vector<PlannedBarrier>& barriers) const;

    RenderGraphResources& m_resources;
    std::vector<Resource> m_res;
    std::vector<Pass> m_passes;
    std::optional<GraphPlan> m_plan;
    bool m_executed = false;
};

} // namespace zodar::gfx
