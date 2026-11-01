#pragma once

#include "Renderer/Material.h"
#include "Renderer/Sampler.h"
#include "Renderer/TextureArray.h"
#include "Renderer/Vec4Table.h"
#include "SceneFile/BlockFile.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace zodar::gfx {
class SceneRenderer;
class UploadContext;
class VulkanContext;
} // namespace zodar::gfx

namespace zodar {

struct BlockMaterialOptions {
    // Sampler for the block textures (pixel art: nearest magnification/minification, linear mips).
    gfx::SamplerDesc sampler = gfx::SamplerDesc::pixelArt();
    bool generateMips = true;
    glm::vec4 baseColorFactor{1.0f}; // multiplies every block's albedo
    std::string name = "Blocks";
};

// GPU resources of a BlockCatalogue for the engine voxel shader "zodar/voxel" (Phase V2):
// a texture array with one layer per catalogue layer, the per-layer parameter table
// (Vec4Table: roughness, metallic, emissive; exact float32) and a material referencing both.
// Chunks meshed with catalogue.palette (and MeshOptions::uvPerMetre = catalogue.uvPerMetre)
// render with material(). Destruction retires everything through the deletion queue.
// Lifetime: like AssetManager, a BlockMaterials must be destroyed before the SceneRenderer
// it was created with (the retired cleanup releases the material through it).
class BlockMaterials {
public:
    // Checks the layer count against the device's maxImageArrayLayers (checkBlockLayerCount),
    // loads every layer texture (sRGB colour), checks the texture-array dimensions (DataError
    // naming each offending layer's JSON path, see checkBlockLayerSizes), uploads and creates
    // the material. `file` names the catalogue's source in errors.
    BlockMaterials(gfx::VulkanContext& context, gfx::UploadContext& upload, gfx::SceneRenderer& scene,
                   const BlockCatalogue& catalogue, std::string_view file = "<memory>",
                   const BlockMaterialOptions& options = {});
    ~BlockMaterials();
    BlockMaterials(const BlockMaterials&) = delete;
    BlockMaterials& operator=(const BlockMaterials&) = delete;

    gfx::MaterialHandle material() const { return m_material; }
    const gfx::TextureArray& textures() const { return m_resources->textures; }
    const gfx::Vec4Table& parameters() const { return m_resources->parameters; }

    // CPU parameter table: one vec4 per layer (roughness, metallic, emissive, 0).
    static std::vector<glm::vec4> layerTable(const BlockCatalogue& catalogue);

private:
    struct Resources {
        gfx::TextureArray textures;
        gfx::Vec4Table parameters;
    };
    gfx::VulkanContext& m_context;
    gfx::SceneRenderer& m_scene;
    std::shared_ptr<Resources> m_resources;
    gfx::MaterialHandle m_material = 0;
};

} // namespace zodar
