#pragma once

#include "Renderer/Material.h"
#include "SceneFile/DataError.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <string_view>

// Material files (*.material.json, Phase B2). Schema: docs/guide/SCENE_FILES.md.
// Example:
//   { "name": "brick", "shader": "zodar/pbr", "baseColor": [0.8, 0.3, 0.2, 1],
//     "roughness": 0.7, "metallic": 0, "emissive": [0, 0, 0] }
// Every field is optional (defaults = gfx::MaterialDesc defaults, shader "zodar/pbr").
// Unknown fields are errors. Textures (Phase C): "baseColorTexture", "metallicRoughnessTexture",
// "normalTexture", "occlusionTexture", "emissiveTexture", each a path string or
//   { "path": "crate.png", "filter": "default|pixelArt|nearest|linear", "wrap": "repeat|clamp|mirror" }
// Paths are relative to the file holding the material. Textures are loaded and bound by the
// AssetManager (Zodar::AssetManager); createMaterial/toMaterialDesc below leave them neutral.
namespace zodar {

enum class MaterialTextureFilter : uint8_t {
    Default,  // the loader's default sampler (AssetManagerDesc::defaultSampler)
    PixelArt, // gfx::SamplerDesc::pixelArt(): nearest mag/min, linear mips
    Nearest,  // gfx::SamplerDesc::nearest()
    Linear,   // gfx::SamplerDesc::linear() (trilinear + anisotropic)
};
enum class MaterialTextureWrap : uint8_t { Repeat, Clamp, Mirror };

// One texture slot of a material file. Empty path: no texture (neutral default).
struct MaterialTextureData {
    std::string path;
    MaterialTextureFilter filter = MaterialTextureFilter::Default;
    MaterialTextureWrap wrap = MaterialTextureWrap::Repeat;

    bool empty() const { return path.empty(); }
    bool operator==(const MaterialTextureData&) const = default;
};

// Sampler for a texture slot; `defaultSampler` is used for MaterialTextureFilter::Default
// (its address modes are replaced by `texture.wrap` in every case).
[[nodiscard]] gfx::SamplerDesc samplerFor(const MaterialTextureData& texture, const gfx::SamplerDesc& defaultSampler);

// CPU description of a material as stored in a file: gfx::MaterialDesc without GPU handles
// (the shader is referenced by its registered name).
struct MaterialData {
    std::string name;
    std::string shader = gfx::MaterialSystem::kPbrShader;
    gfx::AlphaMode alphaMode = gfx::AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    glm::vec4 baseColor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    glm::vec3 emissive{0.0f};
    glm::vec4 custom0{0.0f};
    glm::vec4 custom1{0.0f};
    MaterialTextureData baseColorTexture;
    MaterialTextureData metallicRoughnessTexture; // glTF packing: G = roughness, B = metallic
    MaterialTextureData normalTexture;
    MaterialTextureData occlusionTexture; // R channel
    MaterialTextureData emissiveTexture;

    bool operator==(const MaterialData&) const = default;
};

// Parses material JSON. `file` names the source in error messages; a missing "name" defaults
// to `defaultName`. Throws DataError listing every problem found.
[[nodiscard]] MaterialData parseMaterial(std::string_view json, std::string_view file = "<memory>",
                                         std::string_view defaultName = "");

// Reads and parses a material file; the default name is the file name without
// ".material.json" (or the extension).
[[nodiscard]] MaterialData loadMaterialFile(const std::filesystem::path& path);

// Deterministic, pretty-printed JSON with every field written (texture slots only when set);
// parseMaterial(serialize(m)) == m.
[[nodiscard]] std::string serializeMaterial(const MaterialData& material);

// GPU description for MaterialSystem::createMaterial (textures stay neutral defaults).
[[nodiscard]] gfx::MaterialDesc toMaterialDesc(const MaterialData& material, gfx::ShaderHandle shader);

// Resolves the shader by name and creates the material. Throws DataError (path "/shader")
// for an unregistered shader name; `file` names the source in that message.
gfx::MaterialHandle createMaterial(gfx::MaterialSystem& materials, const MaterialData& material,
                                   std::string_view file = "<memory>");

// Mutable materials: re-applies `material` to an existing handle (e.g. an edited material
// file). The GPU update is deferred to the next recorded frame (MaterialSystem::updateMaterial).
// Throws DataError for an unknown shader or handle, or a pipeline-state change (shader,
// alphaMode, doubleSided).
void updateMaterial(gfx::MaterialSystem& materials, gfx::MaterialHandle handle, const MaterialData& material,
                    std::string_view file = "<memory>");

} // namespace zodar
