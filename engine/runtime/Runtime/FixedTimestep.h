#pragma once

#include <cstdint>

namespace zodar {

// Fixed simulation timestep with an accumulator (no GPU, no clock: callers feed frame times).
//
//   FixedTimestep step({1.0 / 60.0});
//   const uint32_t n = step.advance(frameSeconds);
//   for (uint32_t i = 0; i < n; ++i) simulate(step.fixedDt());
//   render(step.alpha()); // interpolate between the last two simulation states
//
// Long frames are clamped (maxFrameTime) and at most maxSteps steps run per frame; leftover
// time beyond that is dropped so a slow frame cannot cause a "spiral of death".
struct FixedTimestepDesc {
    double fixedDt = 1.0 / 60.0;  // seconds per simulation step (> 0)
    uint32_t maxSteps = 8;        // per advance() call (>= 1)
    double maxFrameTime = 0.25;   // seconds (>= fixedDt); longer frame times are clamped
};

class FixedTimestep {
public:
    explicit FixedTimestep(const FixedTimestepDesc& desc = {});

    // Adds frameSeconds (negative treated as 0) and returns the number of fixed steps to run.
    uint32_t advance(double frameSeconds);

    double fixedDt() const { return m_desc.fixedDt; }
    // Fraction of a step left in the accumulator, in [0, 1): interpolation weight.
    double alpha() const { return m_accumulator / m_desc.fixedDt; }
    // Simulated time: total steps * fixedDt.
    double simulationTime() const { return static_cast<double>(m_steps) * m_desc.fixedDt; }
    uint64_t totalSteps() const { return m_steps; }
    // Seconds dropped because of maxFrameTime / maxSteps clamping (diagnostics).
    double droppedTime() const { return m_dropped; }

private:
    FixedTimestepDesc m_desc;
    double m_accumulator = 0.0;
    double m_dropped = 0.0;
    uint64_t m_steps = 0;
};

} // namespace zodar
