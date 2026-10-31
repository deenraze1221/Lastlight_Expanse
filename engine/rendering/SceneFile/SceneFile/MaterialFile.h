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
// Unknown fields are errors. Textures are not supported yet (Phase C asset manager).
namespace zodar {

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

    bool operator==(const MaterialData&) const = default;
};

// Parses material JSON. `file` names the source in error messages; a missing "name" defaults
// to `defaultName`. Throws DataError listing every problem found.
[[nodiscard]] MaterialData parseMaterial(std::string_view json, std::string_view file = "<memory>",
                                         std::string_view defaultName = "");

// Reads and parses a material file; the default name is the file name without
// ".material.json" (or the extension).
[[nodiscard]] MaterialData loadMaterialFile(const std::filesystem::path& path);

// Deterministic, pretty-printed JSON with every field written; parseMaterial(serialize(m)) == m.
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
