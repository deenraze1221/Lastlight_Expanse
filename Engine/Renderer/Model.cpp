#include "Renderer/Model.h"

#include "Renderer/Resources/UploadContext.h"

namespace zodar::gfx {
namespace {

AddressMode toAddress(ModelSampler::Wrap w) {
    switch (w) {
    case ModelSampler::Wrap::MirroredRepeat: return AddressMode::MirroredRepeat;
    case ModelSampler::Wrap::ClampToEdge: return AddressMode::ClampToEdge;
    default: return AddressMode::Repeat;
    }
}

} // namespace

Model::Model(UploadBatch& batch, SceneRenderer& scene, const ModelData& data, const ModelUploadOptions& options)
    : m_nodes(data.nodes), m_bounds(data.bounds) {
    m_meshes.reserve(data.meshes.size());
    for (const MeshData& mesh : data.meshes) {
        m_meshes.emplace_back(batch, mesh);
    }
    m_textures.reserve(data.images.size());
    for (const ImageData& image : data.images) {
        m_textures.emplace_back(batch, scene.bindless(), image);
    }

    MaterialSystem& materials = scene.materials();
    const ShaderHandle pbr = options.pbrShader.value_or(materials.pbrShader());
    const ShaderHandle unlit = options.unlitShader.value_or(materials.unlitShader());

    const auto binding = [&](const ModelTextureRef& ref) {
        TextureBinding b;
        if (ref.image >= 0) {
            b.texture = &m_textures[static_cast<size_t>(ref.image)];
        }
        if (options.samplerOverride) {
            b.sampler = *options.samplerOverride;
            return b;
        }
        b.sampler = options.defaultSampler;
        if (ref.sampler >= 0) {
            const ModelSampler& s = data.samplers[static_cast<size_t>(ref.sampler)];
            b.sampler.addressU = toAddress(s.wrapU);
            b.sampler.addressV = toAddress(s.wrapV);
            if (s.specified) {
                b.sampler.magFilter = s.magNearest ? Filter::Nearest : Filter::Linear;
                b.sampler.minFilter = s.minNearest ? Filter::Nearest : Filter::Linear;
                b.sampler.mipFilter = s.mipNearest ? Filter::Nearest : Filter::Linear;
                b.sampler.enableMips = s.useMips;
            }
        }
        return b;
    };

    for (const ModelMaterial& m : data.materials) {
        MaterialDesc d;
        d.name = m.name;
        d.shader = m.shading == ModelMaterial::Shading::Unlit ? unlit : pbr;
        d.alphaMode = m.alpha == ModelMaterial::Alpha::Mask    ? AlphaMode::Mask
                      : m.alpha == ModelMaterial::Alpha::Blend ? AlphaMode::Blend
                                                               : AlphaMode::Opaque;
        d.alphaCutoff = m.alphaCutoff;
        d.doubleSided = m.doubleSided;
        d.baseColorFactor = m.baseColorFactor;
        d.metallic = m.metallic;
        d.roughness = m.roughness;
        d.normalScale = m.normalScale;
        d.occlusionStrength = m.occlusionStrength;
        d.emissive = m.emissive;
        d.baseColor = binding(m.baseColor);
        d.metallicRoughness = binding(m.metallicRoughness);
        d.normal = binding(m.normal);
        d.occlusion = binding(m.occlusion);
        d.emissiveTexture = binding(m.emissiveTexture);
        m_materials.push_back(materials.createMaterial(d));
    }
    // glTF default material for primitives without one.
    MaterialDesc fallback;
    fallback.name = data.name + "/default";
    fallback.shader = pbr;
    fallback.metallic = 1.0f;
    fallback.roughness = 1.0f;
    m_fallbackMaterial = materials.createMaterial(fallback);
}

void Model::appendDraws(std::vector<MeshDraw>& draws, const glm::mat4& transform) const {
    for (const ModelNode& node : m_nodes) {
        const GpuMesh& mesh = m_meshes[static_cast<size_t>(node.mesh)];
        for (uint32_t s = 0; s < mesh.submeshes().size(); ++s) {
            const uint32_t materialIndex = mesh.submeshes()[s].materialIndex;
            const MaterialHandle material =
                materialIndex < m_materials.size() ? m_materials[materialIndex] : m_fallbackMaterial;
            draws.push_back({&mesh, s, material, transform * node.world});
        }
    }
}

} // namespace zodar::gfx
