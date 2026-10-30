#pragma once

#include "Assets/ModelData.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Texture.h"

#include <optional>
#include <vector>

namespace zodar::gfx {

class UploadBatch;

// Optional overrides applied when a model is turned into GPU resources. They let a game
// impose its own look (shaders, sampling) on imported assets without engine changes.
struct ModelUploadOptions {
    // Replaces every sampler (e.g. point sampling) instead of the file's/default sampling.
    std::optional<SamplerDesc> samplerOverride;
    // Sampler for textures whose file leaves filtering unspecified.
    SamplerDesc defaultSampler = SamplerDesc::linear();
    // Shaders used for glTF PBR and unlit materials (default: engine built-ins).
    std::optional<ShaderHandle> pbrShader;
    std::optional<ShaderHandle> unlitShader;
};

// GPU-resident model: meshes, textures and materials created from ModelData.
class Model {
public:
    Model(UploadBatch& batch, SceneRenderer& scene, const ModelData& data, const ModelUploadOptions& options = {});

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&) noexcept = default;
    Model& operator=(Model&&) noexcept = default;

    // Appends one draw per node submesh, transformed by `transform`.
    void appendDraws(std::vector<MeshDraw>& draws, const glm::mat4& transform = glm::mat4(1.0f)) const;

    const Bounds& bounds() const { return m_bounds; }
    size_t meshCount() const { return m_meshes.size(); }
    size_t textureCount() const { return m_textures.size(); }
    const std::vector<MaterialHandle>& materials() const { return m_materials; }

private:
    std::vector<GpuMesh> m_meshes;
    std::vector<Texture> m_textures;
    std::vector<MaterialHandle> m_materials;
    MaterialHandle m_fallbackMaterial = 0;
    std::vector<ModelNode> m_nodes;
    Bounds m_bounds;
};

} // namespace zodar::gfx
