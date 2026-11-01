#include "AssetManager/BlockMaterials.h"

#include "Assets/ImageData.h"
#include "Core/Error.h"
#include "Renderer/BindlessRegistry.h"
#include "Renderer/Resources/UploadContext.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <bit>
#include <utility>

namespace zodar {

std::vector<glm::vec4> BlockMaterials::layerTable(const BlockCatalogue& catalogue) {
    std::vector<glm::vec4> table;
    table.reserve(catalogue.layerParams.size());
    for (const BlockLayerParams& p : catalogue.layerParams) {
        table.emplace_back(p.roughness, p.metallic, p.emissive, 0.0f);
    }
    return table;
}

BlockMaterials::BlockMaterials(gfx::VulkanContext& context, gfx::UploadContext& upload, gfx::SceneRenderer& scene,
                               const BlockCatalogue& catalogue, std::string_view file,
                               const BlockMaterialOptions& options)
    : m_context(context), m_scene(scene), m_resources(std::make_shared<Resources>()) {
    ZODAR_ENSURE(!catalogue.layers.empty(), "block materials '" + options.name + "': the catalogue has no layers");
    std::vector<ImageData> images;
    std::vector<glm::uvec2> sizes;
    images.reserve(catalogue.layers.size());
    for (const std::filesystem::path& layer : catalogue.layers) {
        images.push_back(loadImage(layer, ColorSpace::Srgb));
        sizes.emplace_back(images.back().width, images.back().height);
    }
    if (std::vector<DataIssue> issues = checkBlockLayerSizes(catalogue, sizes, file); !issues.empty()) {
        throw DataError(std::move(issues));
    }
    if (options.generateMips) {
        for (ImageData& image : images) image.generateMips();
    }
    const std::vector<glm::vec4> table = layerTable(catalogue);
    {
        gfx::UploadBatch batch(upload);
        m_resources->textures = gfx::TextureArray(batch, scene.bindless(), images, options.name + "Textures");
        m_resources->parameters = gfx::Vec4Table(batch, scene.bindless(), table, options.name + "Layers");
        batch.submit();
    }
    gfx::MaterialDesc desc;
    desc.name = options.name;
    desc.shader = scene.materials().voxelShader();
    desc.baseColorFactor = options.baseColorFactor;
    desc.custom0 = {std::bit_cast<float>(m_resources->textures.bindlessIndex()),
                    std::bit_cast<float>(scene.bindless().sampler(options.sampler)),
                    std::bit_cast<float>(m_resources->parameters.bindlessIndex()), 0.0f};
    m_material = scene.materials().createMaterial(desc);
}

BlockMaterials::~BlockMaterials() {
    gfx::SceneRenderer* scene = &m_scene;
    const gfx::MaterialHandle handle = m_material;
    m_context.deletionQueue().retire([scene, handle, resources = std::move(m_resources)]() mutable {
        scene->materials().releaseMaterial(handle);
        resources.reset();
    });
}

} // namespace zodar
