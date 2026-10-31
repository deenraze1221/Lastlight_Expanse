#include <doctest/doctest.h>

#include "Core/Error.h"
#include "Renderer/RenderGraph/GraphPlanner.h"

using namespace zodar::gfx;

namespace {

PlanResource transient(const char* name) {
    PlanResource r;
    r.name = name;
    return r;
}

PlanResource importedOutput(const char* name) {
    PlanResource r;
    r.name = name;
    r.imported = true;
    r.initial = usageState(ResourceUsage::ColorAttachment);
    r.hasFinalState = true;
    r.final = usageState(ResourceUsage::ColorAttachment);
    return r;
}

const PlannedBarrier* find(const std::vector<PlannedBarrier>& barriers, uint32_t resource) {
    for (const auto& b : barriers) {
        if (b.resource == resource) return &b;
    }
    return nullptr;
}

} // namespace

TEST_CASE("graph planner: scene -> resolve chain with transitions") {
    // 0: hdr colour (transient), 1: depth (transient), 2: swapchain (imported)
    const std::vector<PlanResource> resources = {transient("hdr"), transient("depth"), importedOutput("output")};
    const std::vector<PlanPass> passes = {
        {"Scene", {{0, ResourceUsage::ColorAttachment, true}, {1, ResourceUsage::DepthAttachment, true}}, false},
        {"Resolve", {{0, ResourceUsage::SampledFragment, false}, {2, ResourceUsage::ColorAttachment, true}}, false},
    };
    const GraphPlan plan = planGraph(resources, passes);
    REQUIRE(plan.passes.size() == 2);
    CHECK(plan.culled.empty());

    const PlannedBarrier* hdrIn = find(plan.barriers[0], 0);
    REQUIRE(hdrIn);
    CHECK(hdrIn->src.layout == VK_IMAGE_LAYOUT_UNDEFINED); // Cleared: contents discarded.
    CHECK(hdrIn->dst.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK((hdrIn->src.stages & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) != 0); // Cross-frame reuse ordered.

    const PlannedBarrier* hdrRead = find(plan.barriers[1], 0);
    REQUIRE(hdrRead);
    CHECK(hdrRead->src.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(hdrRead->dst.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(hdrRead->src.access == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT); // Only writes are made available.
    CHECK(hdrRead->dst.stages == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    // Output stays a colour attachment: final barrier only orders the write.
    REQUIRE(plan.finalBarriers.size() == 1);
    CHECK(plan.finalBarriers[0].dst.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

TEST_CASE("graph planner: unused passes are culled, side effects are kept") {
    const std::vector<PlanResource> resources = {transient("unused"), transient("debug"), importedOutput("output")};
    const std::vector<PlanPass> passes = {
        {"WritesUnused", {{0, ResourceUsage::ColorAttachment, true}}, false},
        {"SideEffect", {{1, ResourceUsage::ColorAttachment, true}}, true},
        {"Final", {{2, ResourceUsage::ColorAttachment, true}}, false},
    };
    const GraphPlan plan = planGraph(resources, passes);
    CHECK(plan.culled == std::vector<uint32_t>{0});
    CHECK(plan.passes == std::vector<uint32_t>{1, 2});
    CHECK_FALSE(plan.resourceUsed[0]);
}

TEST_CASE("graph planner: read after read needs no second barrier; write after read waits for readers") {
    const std::vector<PlanResource> resources = {transient("tex"), importedOutput("a"), importedOutput("b")};
    const std::vector<PlanPass> passes = {
        {"Produce", {{0, ResourceUsage::ColorAttachment, true}}, false},
        {"ReadA", {{0, ResourceUsage::SampledFragment, false}, {1, ResourceUsage::ColorAttachment, true}}, false},
        {"ReadB", {{0, ResourceUsage::SampledFragment, false}, {2, ResourceUsage::ColorAttachment, true}}, false},
        {"Overwrite", {{0, ResourceUsage::ColorAttachment, false}, {1, ResourceUsage::ColorAttachment, false}}, false},
    };
    const GraphPlan plan = planGraph(resources, passes);
    REQUIRE(plan.passes.size() == 4);
    CHECK(find(plan.barriers[1], 0) != nullptr);
    CHECK(find(plan.barriers[2], 0) == nullptr); // Same layout, same stage, already visible.
    const PlannedBarrier* war = find(plan.barriers[3], 0);
    REQUIRE(war);
    CHECK((war->src.stages & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT) != 0);
    CHECK(war->src.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // Load: contents preserved.
}

TEST_CASE("graph planner: compute storage buffer RAW and invalid graphs") {
    PlanResource buffer = transient("indirect");
    buffer.isImage = false;
    const std::vector<PlanResource> resources = {buffer, importedOutput("out")};
    const std::vector<PlanPass> passes = {
        {"Cull", {{0, ResourceUsage::StorageWrite, true}}, false},
        {"Draw", {{0, ResourceUsage::IndirectArgs, false}, {1, ResourceUsage::ColorAttachment, true}}, false},
    };
    const GraphPlan plan = planGraph(resources, passes);
    const PlannedBarrier* b = find(plan.barriers[1], 0);
    REQUIRE(b);
    CHECK(b->src.access == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    CHECK(b->dst.access == VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    CHECK(b->dst.layout == VK_IMAGE_LAYOUT_UNDEFINED);

    const std::vector<PlanPass> readFirst = {
        {"Bad", {{0, ResourceUsage::IndirectArgs, false}, {1, ResourceUsage::ColorAttachment, true}}, false}};
    CHECK_THROWS_AS(planGraph(resources, readFirst), zodar::Error);

    const std::vector<PlanResource> images = {transient("img"), importedOutput("out")};
    const std::vector<PlanPass> twoLayouts = {
        {"Make", {{0, ResourceUsage::ColorAttachment, true}}, false},
        {"Bad", {{0, ResourceUsage::SampledFragment, false}, {0, ResourceUsage::ColorAttachment, false},
                 {1, ResourceUsage::ColorAttachment, true}}, false}};
    CHECK_THROWS_AS(planGraph(images, twoLayouts), zodar::Error);
}

TEST_CASE("graph planner: imported resource transitions to its final layout") {
    PlanResource out = importedOutput("swapchain");
    out.final = {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    const std::vector<PlanResource> resources = {out};
    const std::vector<PlanPass> passes = {{"Draw", {{0, ResourceUsage::ColorAttachment, true}}, false}};
    const GraphPlan plan = planGraph(resources, passes);
    REQUIRE(plan.finalBarriers.size() == 1);
    CHECK(plan.finalBarriers[0].src.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(plan.finalBarriers[0].dst.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
