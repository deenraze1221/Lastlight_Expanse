#include "Runtime/FixedTimestep.h"

#include "Core/Error.h"

#include <algorithm>
#include <cmath>

namespace zodar {

FixedTimestep::FixedTimestep(const FixedTimestepDesc& desc) : m_desc(desc) {
    if (!(m_desc.fixedDt > 0.0)) fail("FixedTimestep: fixedDt must be > 0");
    if (m_desc.maxSteps == 0) fail("FixedTimestep: maxSteps must be >= 1");
    if (!(m_desc.maxFrameTime >= m_desc.fixedDt)) fail("FixedTimestep: maxFrameTime must be >= fixedDt");
}

uint32_t FixedTimestep::advance(double frameSeconds) {
    const double clamped = std::clamp(frameSeconds, 0.0, m_desc.maxFrameTime);
    if (frameSeconds > clamped) m_dropped += frameSeconds - clamped;
    m_accumulator += clamped;

    uint32_t steps = 0;
    // Small epsilon so exact multiples (e.g. 2 * 1/60 fed as one frame) are not lost to rounding.
    const double epsilon = m_desc.fixedDt * 1e-9;
    while (m_accumulator + epsilon >= m_desc.fixedDt && steps < m_desc.maxSteps) {
        m_accumulator = std::max(0.0, m_accumulator - m_desc.fixedDt);
        ++steps;
    }
    if (m_accumulator + epsilon >= m_desc.fixedDt) {
        // maxSteps reached: drop whole steps, keep the fractional part for interpolation.
        const double whole = std::floor((m_accumulator + epsilon) / m_desc.fixedDt) * m_desc.fixedDt;
        m_dropped += whole;
        m_accumulator = std::max(0.0, m_accumulator - whole);
    }
    m_steps += steps;
    return steps;
}

} // namespace zodar
