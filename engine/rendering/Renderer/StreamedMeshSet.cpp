#include "Renderer/StreamedMeshSet.h"

#include "Renderer/Mesh.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/Vulkan/VulkanContext.h"

namespace zodar::gfx {

StreamedMeshSet::StreamedMeshSet(VulkanContext& context, UploadContext& upload) : m_context(context), m_upload(upload) {}

StreamedMeshSet::~StreamedMeshSet() {
    if (m_batch) {
        m_batch->submit(); // never leave an unsubmitted batch behind
        m_batch.reset();
    }
    for (auto& [key, slot] : m_slots) {
        retire(std::move(slot.live));
        retire(std::move(slot.pending));
    }
}

void StreamedMeshSet::retire(std::shared_ptr<GpuMesh> mesh) {
    if (mesh) {
        const uint64_t bytes=uint64_t(mesh->vertexCount())*sizeof(Vertex)+uint64_t(mesh->indexCount())*4;
        *m_deferredBytes+=bytes;
        m_context.deletionQueue().retire([mesh=std::move(mesh), counter=m_deferredBytes,bytes]() mutable {
            mesh.reset(); *counter-=bytes;
        });
    }
}

StreamedMeshSet::Stats StreamedMeshSet::stats() const {
    auto result=m_stats; result.deferredBytes=*m_deferredBytes; result.gpuBytes+=result.deferredBytes;
    // Superseded uploads can retain staging outside slots until the retirement callback.
    result.stagingBytes+=result.deferredBytes;
    return result;
}
bool StreamedMeshSet::tryUpsert(uint64_t key,const MeshData& mesh,const glm::mat4& transform,
                              MaterialHandle material,uint64_t budgetBytes) {
    const auto current=stats();
    const uint64_t bytes=mesh.vertices.size()*sizeof(Vertex)+mesh.indices.size()*4;
    if(budgetBytes && current.gpuBytes+current.stagingBytes+2*bytes>budgetBytes) return false;
    upsert(key,mesh,transform,material); return true;
}
void StreamedMeshSet::upsert(uint64_t key, const MeshData& mesh, const glm::mat4& transform, MaterialHandle material) {
    if (mesh.indices.empty()) {
        remove(key);
        return;
    }
    if (!m_batch) {
        m_batch = std::make_unique<UploadBatch>(m_upload);
    }
    Slot& slot = m_slots[key];
    // A pending mesh that is superseded has never been drawn. If its upload is still in the
    // open batch the batch references its buffers, so it is retired like any other mesh.
    accountSlot(slot,false);
    retire(std::move(slot.pending));
    try { slot.pending = std::make_shared<GpuMesh>(*m_batch, mesh); }
    catch(...) { accountSlot(slot,true); throw; }
    slot.pendingTransform = transform;
    slot.pendingMaterial = material;
    slot.ticket = {};
    m_inBatch.push_back(key);
    m_stats.uploadedBytes += mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(uint32_t);
    accountSlot(slot,true);
}

void StreamedMeshSet::remove(uint64_t key) {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return;
    }
    accountSlot(it->second,false);
    retire(std::move(it->second.live));
    retire(std::move(it->second.pending));
    m_slots.erase(it);
}

void StreamedMeshSet::update() {
    if (m_batch) {
        const UploadTicket ticket = m_batch->submitAsync();
        m_batch.reset();
        for (uint64_t key : m_inBatch) {
            auto it = m_slots.find(key);
            if (it != m_slots.end() && it->second.pending && !it->second.ticket.valid()) {
                it->second.ticket = ticket;
            }
        }
        m_inBatch.clear();
    }
    const UploadQueue& queue = m_upload.queue();
    for (auto& [key, slot] : m_slots) {
        if (slot.pending && slot.ticket.valid() && queue.isReady(slot.ticket)) {
            accountSlot(slot,false);
            retire(std::move(slot.live));
            slot.live = std::move(slot.pending);
            slot.liveTransform = slot.pendingTransform;
            slot.liveMaterial = slot.pendingMaterial;
            slot.ticket = {};
            accountSlot(slot,true);
        }
    }
}

void StreamedMeshSet::accountSlot(const Slot& slot, bool add) {
    const auto adjust=[add](auto& total,auto value) { if(add) total+=value; else total-=value; };
    if(slot.live) {
        adjust(m_stats.meshes,uint32_t(1));
        adjust(m_stats.triangles,uint64_t(slot.live->indexCount()/3));
        adjust(m_stats.gpuBytes,uint64_t(slot.live->vertexCount())*sizeof(Vertex)+uint64_t(slot.live->indexCount())*4);
    }
    if(slot.pending) {
        const uint64_t bytes=uint64_t(slot.pending->vertexCount())*sizeof(Vertex)+uint64_t(slot.pending->indexCount())*4;
        adjust(m_stats.pendingUploads,uint32_t(1));
        adjust(m_stats.stagingBytes,bytes);
        adjust(m_stats.gpuBytes,bytes);
    }
}

void StreamedMeshSet::appendDraws(std::vector<MeshDraw>& out) const {
    out.reserve(out.size() + m_slots.size());
    for (const auto& [key, slot] : m_slots) {
        if (slot.live) {
            out.push_back({slot.live.get(), 0, slot.liveMaterial, slot.liveTransform});
        }
    }
}

} // namespace zodar::gfx
