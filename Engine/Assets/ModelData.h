#pragma once

#include "Assets/Bounds.h"
#include "Assets/ImageData.h"
#include "Assets/MeshData.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace raven {

// Renderer-independent sampler state as authored in the source file.
struct ModelSampler {
    enum class Wrap { Repeat, MirroredRepeat, ClampToEdge };
    bool magNearest = false;
    bool minNearest = false;
    bool mipNearest = false;
    bool useMips = true;
    bool specified = false; // false: the file left filtering to the engine/game.
    Wrap wrapU = Wrap::Repeat;
    Wrap wrapV = Wrap::Repeat;
};

struct ModelTextureRef {
    int image = -1;   // Index into ModelData::images, -1 = none.
    int sampler = -1; // Index into ModelData::samplers, -1 = default.
};

struct ModelMaterial {
    enum class Shading { Pbr, Unlit };
    enum class Alpha { Opaque, Mask, Blend };

    std::string name;
    Shading shading = Shading::Pbr;
    Alpha alpha = Alpha::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    glm::vec4 baseColorFactor{1.0f};
    float metallic = 1.0f;  // glTF defaults.
    float roughness = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    glm::vec3 emissive{0.0f};
    ModelTextureRef baseColor, metallicRoughness, normal, occlusion, emissiveTexture;
};

struct ModelNode {
    std::string name;
    int mesh = -1;
    glm::mat4 world{1.0f};
};

// Imported static model: meshes (one submesh per primitive, materialIndex refers to
// `materials`), decoded images, materials and a flattened list of mesh instances.
struct ModelData {
    std::string name;
    std::filesystem::path source;
    std::vector<MeshData> meshes;
    std::vector<ImageData> images;
    std::vector<ModelSampler> samplers;
    std::vector<ModelMaterial> materials;
    std::vector<ModelNode> nodes;
    Bounds bounds;
    std::vector<std::string> warnings; // Unsupported features that were skipped.
};

struct GltfLoadOptions {
    bool generateMips = true;
};

// Loads .gltf (external or embedded buffers/images) and .glb files. Supports static
// triangle meshes, metallic-roughness materials, KHR_materials_unlit and
// KHR_materials_emissive_strength. Skins, morph targets, animations, cameras, lights and
// texture transforms are reported in `warnings`. Throws raven::Error on invalid files.
ModelData loadGltf(const std::filesystem::path& path, const GltfLoadOptions& options = {});

} // namespace raven
