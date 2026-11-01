#pragma once

#include "Assets/MeshData.h"

#include <cstddef>
#include <cstdint>

namespace zodar::test {

// FNV-1a 64-bit over raw bytes. Shared by the golden-hash regression tests (mesher output,
// block catalogues); the byte layout of hashed values is part of every golden hash.
class Fnv {
public:
    void bytes(const void* data, size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            m_h = (m_h ^ p[i]) * 0x100000001B3ull;
        }
    }
    template <class T> void value(const T& v) { bytes(&v, sizeof(T)); }
    uint64_t get() const { return m_h; }

private:
    uint64_t m_h = 0xCBF29CE484222325ull;
};

// Vertex and index counts, vertex and index bytes and, when `submeshes` is set, the submesh
// ranges. (The game's Highlands goldens hash without submeshes.)
inline void hashMesh(Fnv& h, const MeshData& mesh, bool submeshes = true) {
    h.value(static_cast<uint64_t>(mesh.vertices.size()));
    h.value(static_cast<uint64_t>(mesh.indices.size()));
    if (!mesh.vertices.empty()) h.bytes(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
    if (!mesh.indices.empty()) h.bytes(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
    if (!submeshes) return;
    h.value(static_cast<uint64_t>(mesh.submeshes.size()));
    for (const Submesh& s : mesh.submeshes) {
        h.value(s.firstIndex);
        h.value(s.indexCount);
    }
}

inline uint64_t meshHash(const MeshData& mesh) {
    Fnv h;
    hashMesh(h, mesh);
    return h.get();
}

} // namespace zodar::test
