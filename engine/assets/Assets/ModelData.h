#pragma once

#include "Assets/Bounds.h"
#include "Assets/ImageData.h"
#include "Assets/MeshData.h"
#include "Assets/TextureCache.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace zodar {

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

// Where a model's textures came from (see GltfLoadOptions).
struct TextureLoadStats {
    uint32_t compiled = 0;    // KTX2 from the compiled cache (baked mips, BCn)
    uint32_t source = 0;      // decoded from the source image (PNG/JPEG)
    uint32_t runtimeMips = 0; // ... with mips generated at load time (fallback)
    uint32_t stale = 0;       // cache files ignored because the source changed
    uint32_t lossyRejected = 0; // block-compressed caches ignored for pixel-art textures
};

// Source of one entry of ModelData::images (GltfLoadOptions::keepImageSources).
struct ModelImageSource {
    std::vector<uint8_t> bytes; // Encoded source file (PNG/JPEG/...)
    TextureUsage usage = TextureUsage::Color;
    int gltfImage = -1;
    std::filesystem::path cachePath; // Where the compiled KTX2 belongs
    std::string sourceKey;           // textureSourceKey(bytes, usage)
    bool pixelArt = false;           // must be compiled losslessly (see GltfLoadOptions::pixelArt)
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
    TextureLoadStats textureStats;
    std::vector<ModelImageSource> imageSources; // Parallel to images when requested.
};

struct GltfLoadOptions {
    // Use compiled KTX2 textures (baked mips, BCn) from textureCacheDirectory(path) when
    // present and up to date; otherwise decode the source image.
    bool useCompiledTextures = true;
    // For decoded source images only: generate mips at load time (fallback; compiled
    // textures carry baked mips).
    bool generateMips = true;
    bool keepImageSources = false; // Fill ModelData::imageSources (asset compiler)
    // Pixel art: exact texel colours are required, so lossy block-compressed (BCn) compiled
    // textures are never used (rejected caches count in TextureLoadStats::lossyRejected; the
    // source image is decoded instead). Textures whose glTF sampler magnifies with NEAREST
    // (Blockbench exports) are pixel art automatically; true marks every texture.
    bool pixelArt = false;
};

// Loads .gltf (external or embedded buffers/images) and .glb files. Supports static
// triangle meshes, metallic-roughness materials, KHR_materials_unlit and
// KHR_materials_emissive_strength. Skins, morph targets, animations, cameras, lights and
// texture transforms are reported in `warnings`. Throws zodar::Error on invalid files.
ModelData loadGltf(const std::filesystem::path& path, const GltfLoadOptions& options = {});

} // namespace zodar
