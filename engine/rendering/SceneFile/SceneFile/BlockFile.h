#pragma once

#include "SceneFile/DataError.h"
#include "Scene/Voxel/BlockRegistry.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Block definition files (*.blocks.json, Phase V2; docs/guide/VOXELS.md §3). Game content:
// materials and block states by name, loaded by the engine into a voxel block registry.
//
//   { "schema": "zodar.blocks", "version": 1, "uvPerMetre": 1,
//     "materials": [
//       { "name": "concrete", "textures": "textures/concrete.png", "roughness": 0.9 },
//       { "name": "panel", "textures": { "top": "panel_top.png", "side": "panel.png", "bottom": "panel.png" },
//         "tint": [1, 1, 1], "metallic": 0.6, "roughness": 0.4, "emissive": 2 } ],
//     "blocks": [
//       { "name": "concrete", "material": "concrete" },
//       { "name": "stair", "material": "concrete", "shape": "stair", "rotations": 4 },
//       { "name": "grate", "material": "panel", "shape": "slab_low", "collision": false } ] }
//
// Block ids follow the file order: id 0 is empty, the first block is id 1. "rotations": 4
// expands one definition into four consecutive ids named "<name>", "<name>@1", "<name>@2",
// "<name>@3" (yaw steps 0..3); '@' is reserved in names. Ids therefore depend on the file
// order: reordering, inserting or removing a definition changes every later id (see the id
// stability rules in VOXELS §3: append only until regions save names, V4).
namespace zodar {

struct BlockMaterialData {
    std::string name;
    // Texture per face group: top (+Y), side, bottom (-Y); relative to the block file.
    std::array<std::string, 3> textures;
    glm::vec3 tint{1.0f};    // multiplies the texel colour (palette colour of the material)
    float roughness = 0.8f;  // [0, 1]
    float metallic = 0.0f;   // [0, 1]
    float emissive = 0.0f;   // >= 0: strength of emissive texels (texel alpha < 1)
    bool operator==(const BlockMaterialData&) const = default;
};

struct BlockDefinitionData {
    std::string name;
    std::string material;
    std::string shape = "cube"; // shape library name
    uint8_t rotations = 1;      // 1 or 4 (expanded into name@1..@3)
    bool collision = true;      // false: BlockState::kNoCollision
    bool operator==(const BlockDefinitionData&) const = default;
};

struct BlockFileData {
    static constexpr const char* kSchema = "zodar.blocks";
    static constexpr int kVersion = 1;

    float uvPerMetre = 1.0f; // texture tiles per metre (MeshOptions::uvPerMetre)
    std::vector<BlockMaterialData> materials;
    std::vector<BlockDefinitionData> blocks;
    bool operator==(const BlockFileData&) const = default;
};

// Parses block JSON; `file` names the source in errors. Throws DataError listing every problem
// with its JSON path (and line:column): schema/version, unknown fields, duplicate or reserved
// names, unknown materials, bad numbers, rotations other than 1/4, more than 65535 ids. With
// `shapes`, unknown shape names are reported too.
[[nodiscard]] BlockFileData parseBlockFile(std::string_view json, std::string_view file,
                                           const voxel::ShapeLibrary* shapes = nullptr);
[[nodiscard]] BlockFileData loadBlockFile(const std::filesystem::path& path, const voxel::ShapeLibrary* shapes = nullptr);
// Deterministic, pretty-printed; parseBlockFile(serializeBlockFile(d)) == d.
[[nodiscard]] std::string serializeBlockFile(const BlockFileData& data);
// Texture files referenced by the file (resolved against `directory`), sorted and unique.
[[nodiscard]] std::vector<std::filesystem::path> blockFileTextures(const BlockFileData& data,
                                                                   const std::filesystem::path& directory);

// Per texture-array layer shading parameters (zodar/voxel reads them by layer).
struct BlockLayerParams {
    float roughness = 0.8f;
    float metallic = 0.0f;
    float emissive = 0.0f;
    bool operator==(const BlockLayerParams&) const = default;
};

// A loaded block registry: names, ids, the voxel palette and the texture-array layers.
struct BlockCatalogue {
    // id -> name ("" for id 0); size = palette.blocks.size().
    std::vector<std::string> names;
    std::map<std::string, voxel::Voxel, std::less<>> ids; // name -> id
    // Registry-mode palette: colors = material tints, faceLayers = layer per face group,
    // blocks = states per id, shapes = the shape library used to resolve names.
    voxel::VoxelPalette palette;
    // Texture-array layers in first-use order (material order, then top, side, bottom),
    // deduplicated by (texture, parameters). Paths are resolved and normalized.
    std::vector<std::filesystem::path> layers;
    std::vector<BlockLayerParams> layerParams;
    // JSON path of each layer's first use ("/materials/2/textures/side"), for size errors.
    std::vector<std::string> layerOrigins;
    float uvPerMetre = 1.0f;

    // Block id by name ("stair@2"); nullopt for unknown names.
    std::optional<voxel::Voxel> find(std::string_view name) const;
};

// Builds the catalogue. `directory` resolves texture paths; `shapes` (default: the built-in
// library) resolves shape names (throws DataError naming "/blocks/<i>/shape" for an unknown
// one). Deterministic: the same data gives the same catalogue.
[[nodiscard]] BlockCatalogue buildBlockCatalogue(const BlockFileData& data, const std::filesystem::path& directory,
                                                 std::shared_ptr<const voxel::ShapeLibrary> shapes = {},
                                                 std::string_view file = "<memory>");
[[nodiscard]] BlockCatalogue loadBlockCatalogue(const std::filesystem::path& path,
                                                std::shared_ptr<const voxel::ShapeLibrary> shapes = {});

// Texture-array dimension check: every layer must be square and the same size as layer 0.
// `sizes[i]` is the (width, height) of layers[i]. Returns one issue per offending layer, with
// the layer's JSON path, e.g. "/materials/1/textures/side: 32x32 (rust.png) differs from the
// texture array size 64x64 (first layer: /materials/0/textures/top)".
[[nodiscard]] std::vector<DataIssue> checkBlockLayerSizes(const BlockCatalogue& catalogue,
                                                          const std::vector<glm::uvec2>& sizes, std::string_view file);

} // namespace zodar
