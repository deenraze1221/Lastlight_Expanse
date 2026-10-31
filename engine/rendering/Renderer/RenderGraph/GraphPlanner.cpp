#include "Renderer/RenderGraph/GraphPlanner.h"

#include "Core/Error.h"

#include <algorithm>

namespace zodar::gfx {
namespace {

constexpr VkAccessFlags2 kWriteAccess =
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
    VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT;

// Synchronization state of one resource while walking the passes in order.
struct Track {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 writeStages = 0; // Last write (or layout transition).
    VkAccessFlags2 writeAccess = 0;        // Memory that must be made available.
    VkPipelineStageFlags2 readStages = 0;  // Readers since the last write (for WAR).
    VkPipelineStageFlags2 visibleStages = 0; // Stages/accesses that already see the last write.
    VkAccessFlags2 visibleAccess = 0;
    bool written = false;
};

} // namespace

GraphPlan planGraph(const std::vector<PlanResource>& resources, const std::vector<PlanPass>& passes) {
    GraphPlan plan;

    // --- Culling: walk backwards from externally visible resources and side effects.
    std::vector<bool> neededResource(resources.size(), false);
    for (size_t r = 0; r < resources.size(); ++r) {
        neededResource[r] = resources[r].imported;
    }
    std::vector<bool> neededPass(passes.size(), false);
    for (size_t p = passes.size(); p-- > 0;) {
        bool needed = passes[p].sideEffect;
        for (const PlanUse& use : passes[p].uses) {
            ZODAR_ENSURE(use.resource < resources.size(), "pass '" + passes[p].name + "' uses an unknown resource");
            if (isWriteUsage(use.usage) && neededResource[use.resource]) {
                needed = true;
            }
        }
        neededPass[p] = needed;
        if (needed) {
            for (const PlanUse& use : passes[p].uses) {
                neededResource[use.resource] = true; // Its producers are needed too.
            }
        }
    }

    // --- Barrier generation in declaration order.
    std::vector<Track> tracks(resources.size());
    for (size_t r = 0; r < resources.size(); ++r) {
        Track& t = tracks[r];
        if (resources[r].imported) {
            const AccessState& s = resources[r].initial;
            t.layout = s.layout;
            t.written = true;
            if (s.access & kWriteAccess) {
                t.writeStages = s.stages;
                t.writeAccess = s.access & kWriteAccess;
            } else {
                t.readStages = s.stages;
                t.visibleStages = s.stages;
                t.visibleAccess = s.access;
            }
        } else {
            // Transient memory is reused across frames: order against any previous use.
            t.writeStages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            t.writeAccess = VK_ACCESS_2_MEMORY_WRITE_BIT;
            t.readStages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
    }
    plan.resourceUsed.assign(resources.size(), false);

    for (size_t p = 0; p < passes.size(); ++p) {
        if (!neededPass[p]) {
            plan.culled.push_back(static_cast<uint32_t>(p));
            continue;
        }
        const PlanPass& pass = passes[p];

        // Merge multiple uses of the same resource within one pass.
        struct Merged {
            uint32_t resource;
            AccessState state;
            bool write;
            bool discard;
        };
        std::vector<Merged> merged;
        for (const PlanUse& use : pass.uses) {
            const AccessState s = usageState(use.usage);
            auto it = std::find_if(merged.begin(), merged.end(), [&](const Merged& m) { return m.resource == use.resource; });
            if (it == merged.end()) {
                merged.push_back({use.resource, s, isWriteUsage(use.usage), use.discard});
                continue;
            }
            if (resources[use.resource].isImage && it->state.layout != s.layout) {
                fail("pass '" + pass.name + "' uses image '" + resources[use.resource].name + "' in two layouts");
            }
            it->state.stages |= s.stages;
            it->state.access |= s.access;
            it->write = it->write || isWriteUsage(use.usage);
            it->discard = it->discard && use.discard;
        }

        std::vector<PlannedBarrier> barriers;
        for (const Merged& m : merged) {
            const PlanResource& res = resources[m.resource];
            Track& t = tracks[m.resource];
            plan.resourceUsed[m.resource] = true;
            if (!res.imported && !t.written && !m.write) {
                fail("pass '" + pass.name + "' reads transient '" + res.name + "' before anything writes it");
            }
            const bool layoutChange = res.isImage && m.state.layout != t.layout;
            const bool covered = (m.state.stages & ~t.visibleStages) == 0 && (m.state.access & ~t.visibleAccess) == 0;
            const bool needBarrier = m.write || layoutChange || (t.writeStages != 0 && !covered);

            if (needBarrier) {
                PlannedBarrier b;
                b.resource = m.resource;
                b.src.stages = t.writeStages;
                b.src.access = t.writeAccess;
                if (m.write || layoutChange) {
                    b.src.stages |= t.readStages; // Write-after-read needs an execution dependency.
                }
                b.src.layout = (m.discard || !t.written) ? VK_IMAGE_LAYOUT_UNDEFINED : t.layout;
                b.dst = m.state;
                if (!res.isImage) {
                    b.src.layout = b.dst.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                }
                barriers.push_back(b);
            }

            if (m.write) {
                t.writeStages = m.state.stages;
                t.writeAccess = m.state.access & kWriteAccess;
                t.readStages = 0;
                t.visibleStages = m.state.stages;
                t.visibleAccess = m.state.access;
                t.written = true;
            } else if (layoutChange) {
                // The transition is a write that is complete for the barrier's destination only.
                t.writeStages = m.state.stages;
                t.writeAccess = 0;
                t.readStages = m.state.stages;
                t.visibleStages = m.state.stages;
                t.visibleAccess = m.state.access;
            } else {
                t.readStages |= m.state.stages;
                if (needBarrier) {
                    t.visibleStages |= m.state.stages;
                    t.visibleAccess |= m.state.access;
                }
            }
            if (res.isImage) {
                t.layout = m.state.layout;
            }
        }
        plan.passes.push_back(static_cast<uint32_t>(p));
        plan.barriers.push_back(std::move(barriers));
    }

    // --- Imported resources leave the graph in their requested state.
    for (size_t r = 0; r < resources.size(); ++r) {
        const PlanResource& res = resources[r];
        if (!res.imported || !res.hasFinalState) {
            continue;
        }
        const Track& t = tracks[r];
        const bool layoutChange = res.isImage && t.layout != res.final.layout;
        if (!layoutChange && t.writeAccess == 0 && t.readStages == 0) {
            continue;
        }
        PlannedBarrier b;
        b.resource = static_cast<uint32_t>(r);
        b.src = {t.writeStages | t.readStages, t.writeAccess, res.isImage ? t.layout : VK_IMAGE_LAYOUT_UNDEFINED};
        b.dst = res.final;
        if (!res.isImage) {
            b.dst.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        plan.finalBarriers.push_back(b);
    }
    return plan;
}

} // namespace zodar::gfx
