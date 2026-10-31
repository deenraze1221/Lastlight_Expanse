#include "Scene/Voxel/VoxelChunk.h"

#include <algorithm>

namespace zodar::voxel {

void VoxelChunk::set(int x, int y, int z, Voxel v) {
    if (m_data.empty()) {
        if (v == m_fill) {
            return;
        }
        m_data.assign(kChunkVolume, m_fill);
    }
    m_data[index(x, y, z)] = v;
}

void VoxelChunk::fillLayer(int y, Voxel v) {
    if (m_data.empty()) {
        if (v == m_fill) {
            return;
        }
        m_data.assign(kChunkVolume, m_fill);
    }
    const auto begin = m_data.begin() + (y << (2 * kChunkShift));
    std::fill(begin, begin + kChunkSize * kChunkSize, v);
}

bool VoxelChunk::isEmpty() const {
    if (m_data.empty()) {
        return m_fill == kEmpty;
    }
    return std::all_of(m_data.begin(), m_data.end(), [](Voxel v) { return v == kEmpty; });
}

bool VoxelChunk::isFull() const {
    if (m_data.empty()) {
        return m_fill != kEmpty;
    }
    return std::none_of(m_data.begin(), m_data.end(), [](Voxel v) { return v == kEmpty; });
}

void VoxelChunk::compact() {
    if (m_data.empty()) {
        return;
    }
    const Voxel first = m_data.front();
    if (std::all_of(m_data.begin(), m_data.end(), [first](Voxel v) { return v == first; })) {
        m_data.clear();
        m_data.shrink_to_fit();
        m_fill = first;
    }
}

} // namespace zodar::voxel
