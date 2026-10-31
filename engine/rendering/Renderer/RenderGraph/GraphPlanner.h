#pragma once

#include "Renderer/RenderGraph/ResourceState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zodar::gfx {

// Pure planning stage of the render graph: pass culling and barrier generation.
// No Vulkan objects are touched, so the synchronization logic is unit tested directly.

struct PlanResource {
    std::string name;
    bool isImage = true;
    bool imported = false;      // Imported resources are visible outside the graph (never culled).
    AccessState initial;        // Imported: state on entry. Transient: ignored (contents undefined).
    bool hasFinalState = false; // Imported: transition to `final` after the last use.
    AccessState final;
};

struct PlanUse {
    uint32_t resource = 0;
    ResourceUsage usage = ResourceUsage::SampledFragment;
    bool discard = false; // Previous contents are not needed (attachment clear / don't-care).
};

struct PlanPass {
    std::string name;
    std::vector<PlanUse> uses;
    bool sideEffect = false; // Never culled (e.g. readback, external writes).
};

struct PlannedBarrier {
    uint32_t resource = 0;
    AccessState src; // src.layout = old layout
    AccessState dst; // dst.layout = new layout
};

struct GraphPlan {
    std::vector<uint32_t> passes;                       // Executed passes in order.
    std::vector<std::vector<PlannedBarrier>> barriers;  // Barriers before each executed pass.
    std::vector<PlannedBarrier> finalBarriers;          // Imported resources to their final state.
    std::vector<uint32_t> culled;                       // Passes removed (no needed output).
    std::vector<bool> resourceUsed;                     // Per resource: referenced by an executed pass.
};

// Throws zodar::Error for invalid graphs (e.g. reading a transient before any write, or
// one pass using an image in two different layouts).
GraphPlan planGraph(const std::vector<PlanResource>& resources, const std::vector<PlanPass>& passes);

} // namespace zodar::gfx
