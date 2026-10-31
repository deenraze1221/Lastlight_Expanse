#include "Assets/ModelData.h"

#include "Assets/Ktx2.h"

#include "Core/Error.h"
#include "Core/Log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <utility>

namespace zodar {
namespace {

const char* resultName(cgltf_result r) {
    switch (r) {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF 1.0 is not supported";
    default: return "unknown error";
    }
}

struct DataDeleter {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};

class Importer {
public:
    Importer(const std::filesystem::path& path, const GltfLoadOptions& options) : m_path(path), m_options(options) {}

    ModelData run() {
        cgltf_options options{};
        cgltf_data* raw = nullptr;
        const std::string file = m_path.string();
        cgltf_result r = cgltf_parse_file(&options, file.c_str(), &raw);
        if (r != cgltf_result_success) {
            fail("Cannot load glTF '" + file + "': " + resultName(r));
        }
        m_data.reset(raw);
        r = cgltf_load_buffers(&options, raw, file.c_str());
        if (r != cgltf_result_success) {
            fail("Cannot load buffers of glTF '" + file + "': " + resultName(r));
        }
        r = cgltf_validate(raw);
        if (r != cgltf_result_success) {
            fail("glTF '" + file + "' failed validation: " + resultName(r));
        }

        m_model.name = m_path.stem().string();
        m_model.source = m_path;
        reportUnsupported();
        for (cgltf_size i = 0; i < raw->samplers_count; ++i) {
            m_model.samplers.push_back(convertSampler(raw->samplers[i]));
        }
        for (cgltf_size i = 0; i < raw->materials_count; ++i) {
            m_model.materials.push_back(convertMaterial(raw->materials[i]));
        }
        for (cgltf_size i = 0; i < raw->meshes_count; ++i) {
            m_model.meshes.push_back(convertMesh(raw->meshes[i]));
        }
        collectNodes();
        for (const auto& w : m_model.warnings) {
            ZODAR_LOG_WARN("glTF '{}': {}", m_model.name, w);
        }
        return std::move(m_model);
    }

private:
    void warn(std::string message) {
        for (const auto& w : m_model.warnings) {
            if (w == message) return;
        }
        m_model.warnings.push_back(std::move(message));
    }

    void reportUnsupported() {
        const cgltf_data* d = m_data.get();
        if (d->skins_count) warn("skins are not supported yet (meshes are imported in bind pose)");
        if (d->animations_count) warn("animations are not supported yet");
        if (d->cameras_count) warn("cameras are ignored");
        if (d->lights_count) warn("KHR_lights_punctual lights are ignored");
        for (cgltf_size i = 0; i < d->extensions_required_count; ++i) {
            const std::string ext = d->extensions_required[i];
            if (ext != "KHR_materials_unlit" && ext != "KHR_materials_emissive_strength" &&
                ext != "KHR_texture_transform") {
                fail("glTF '" + m_path.string() + "' requires unsupported extension " + ext);
            }
        }
    }

    static ModelSampler::Wrap wrap(cgltf_wrap_mode mode) {
        switch (mode) {
        case cgltf_wrap_mode_clamp_to_edge: return ModelSampler::Wrap::ClampToEdge;
        case cgltf_wrap_mode_mirrored_repeat: return ModelSampler::Wrap::MirroredRepeat;
        default: return ModelSampler::Wrap::Repeat;
        }
    }

    static ModelSampler convertSampler(const cgltf_sampler& s) {
        ModelSampler out;
        out.wrapU = wrap(s.wrap_s);
        out.wrapV = wrap(s.wrap_t);
        out.specified = s.mag_filter != cgltf_filter_type_undefined || s.min_filter != cgltf_filter_type_undefined;
        out.magNearest = s.mag_filter == cgltf_filter_type_nearest;
        switch (s.min_filter) {
        case cgltf_filter_type_nearest: out.minNearest = true; out.useMips = false; break;
        case cgltf_filter_type_linear: out.useMips = false; break;
        case cgltf_filter_type_nearest_mipmap_nearest: out.minNearest = out.mipNearest = true; break;
        case cgltf_filter_type_linear_mipmap_nearest: out.mipNearest = true; break;
        case cgltf_filter_type_nearest_mipmap_linear: out.minNearest = true; break;
        default: break;
        }
        return out;
    }

    // Decodes (once per colour space) the image behind a texture reference.
    ModelTextureRef textureRef(const cgltf_texture_view& view, TextureUsage usage) {
        ModelTextureRef ref;
        if (!view.texture) {
            return ref;
        }
        if (view.texcoord != 0) warn("only TEXCOORD_0 is supported; other texture coordinate sets use TEXCOORD_0");
        if (view.has_transform) warn("KHR_texture_transform is not supported yet and is ignored");
        const cgltf_texture& tex = *view.texture;
        if (tex.sampler) {
            ref.sampler = static_cast<int>(tex.sampler - m_data->samplers);
        }
        const cgltf_image* image = tex.image;
        if (!image) {
            warn("texture '" + std::string(tex.name ? tex.name : "") + "' uses an unsupported image format (KTX2/WebP)");
            return ref;
        }
        const auto key = std::make_pair(image, usage);
        if (auto it = m_imageIndex.find(key); it != m_imageIndex.end()) {
            ref.image = it->second;
            return ref;
        }
        const int sourceIndex = static_cast<int>(image - m_data->images);
        const std::string name = image->name ? image->name : (image->uri ? image->uri : "embedded");
        std::vector<uint8_t> bytes = sourceBytes(*image, name);
        const std::string sourceKey = textureSourceKey(bytes, usage);
        const std::filesystem::path cache = textureCachePath(m_path, sourceIndex, usage);

        ImageData loaded;
        bool compiled = false;
        if (m_options.useCompiledTextures && std::filesystem::exists(cache)) {
            Ktx2File file = loadKtx2(cache);
            const auto it = file.keyValues.find(kTextureSourceKey);
            if (it != file.keyValues.end() && it->second == sourceKey && file.image.colorSpace == colorSpaceFor(usage)) {
                loaded = std::move(file.image);
                loaded.name = name;
                compiled = true;
                ++m_model.textureStats.compiled;
            } else {
                ++m_model.textureStats.stale;
                warn("compiled texture '" + cache.filename().string() + "' is out of date; using the source image");
            }
        }
        if (!compiled) {
            loaded = loadImageFromMemory(bytes, colorSpaceFor(usage), name);
            ++m_model.textureStats.source;
            if (m_options.generateMips) {
                loaded.generateMips(mipFilterFor(usage)); // Runtime fallback: compile the model to bake mips.
                ++m_model.textureStats.runtimeMips;
            }
        }
        ref.image = static_cast<int>(m_model.images.size());
        m_model.images.push_back(std::move(loaded));
        if (m_options.keepImageSources) {
            m_model.imageSources.push_back({std::move(bytes), usage, sourceIndex, cache, sourceKey});
        }
        m_imageIndex.emplace(key, ref.image);
        return ref;
    }

    std::vector<uint8_t> sourceBytes(const cgltf_image& image, const std::string& name) {
        if (image.buffer_view) {
            const auto* bytes = static_cast<const uint8_t*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
            return {bytes, bytes + image.buffer_view->size};
        }
        ZODAR_ENSURE(image.uri != nullptr, "glTF image has neither a URI nor a buffer view");
        const std::string uri = image.uri;
        if (uri.rfind("data:", 0) == 0) {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos || uri.find(";base64") == std::string::npos) {
                fail("Unsupported data URI for image '" + name + "'");
            }
            const std::string base64 = uri.substr(comma + 1);
            const cgltf_size size = (base64.size() / 4) * 3 - (base64.ends_with("==") ? 2 : base64.ends_with("=") ? 1 : 0);
            cgltf_options options{};
            void* data = nullptr;
            if (cgltf_load_buffer_base64(&options, size, base64.c_str(), &data) != cgltf_result_success) {
                fail("Invalid base64 data for image '" + name + "'");
            }
            std::unique_ptr<void, decltype(&std::free)> owner(data, &std::free);
            const auto* bytes = static_cast<const uint8_t*>(data);
            return {bytes, bytes + size};
        }
        std::string decoded = uri;
        cgltf_decode_uri(decoded.data());
        decoded.resize(std::strlen(decoded.c_str()));
        const std::filesystem::path file = m_path.parent_path() / decoded;
        std::ifstream in(file, std::ios::binary);
        if (!in) fail("Cannot open image '" + file.string() + "'");
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    ModelMaterial convertMaterial(const cgltf_material& m) {
        ModelMaterial out;
        out.name = m.name ? m.name : "";
        out.shading = m.unlit ? ModelMaterial::Shading::Unlit : ModelMaterial::Shading::Pbr;
        out.alpha = m.alpha_mode == cgltf_alpha_mode_mask    ? ModelMaterial::Alpha::Mask
                    : m.alpha_mode == cgltf_alpha_mode_blend ? ModelMaterial::Alpha::Blend
                                                             : ModelMaterial::Alpha::Opaque;
        out.alphaCutoff = m.alpha_cutoff;
        out.doubleSided = m.double_sided;
        if (m.has_pbr_specular_glossiness) warn("KHR_materials_pbrSpecularGlossiness is not supported; using base values");
        if (m.has_pbr_metallic_roughness) {
            const auto& pbr = m.pbr_metallic_roughness;
            out.baseColorFactor = glm::make_vec4(pbr.base_color_factor);
            out.metallic = pbr.metallic_factor;
            out.roughness = pbr.roughness_factor;
            out.baseColor = textureRef(pbr.base_color_texture, TextureUsage::Color);
            out.metallicRoughness = textureRef(pbr.metallic_roughness_texture, TextureUsage::Data);
        }
        out.normal = textureRef(m.normal_texture, TextureUsage::Normal);
        out.normalScale = m.normal_texture.texture ? m.normal_texture.scale : 1.0f;
        out.occlusion = textureRef(m.occlusion_texture, TextureUsage::Data);
        out.occlusionStrength = m.occlusion_texture.texture ? m.occlusion_texture.scale : 1.0f;
        out.emissiveTexture = textureRef(m.emissive_texture, TextureUsage::Color);
        out.emissive = glm::make_vec3(m.emissive_factor) *
                       (m.has_emissive_strength ? m.emissive_strength.emissive_strength : 1.0f);
        if (m.has_clearcoat || m.has_transmission || m.has_volume || m.has_sheen || m.has_specular || m.has_ior ||
            m.has_iridescence || m.has_anisotropy) {
            warn("advanced PBR material extensions are ignored");
        }
        return out;
    }

    MeshData convertMesh(const cgltf_mesh& mesh) {
        MeshData out;
        out.name = mesh.name ? mesh.name : ("mesh" + std::to_string(&mesh - m_data->meshes));
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& prim = mesh.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) {
                warn("non-triangle primitives (points/lines/strips/fans) are skipped");
                continue;
            }
            if (prim.has_draco_mesh_compression) {
                fail("glTF '" + m_path.string() + "' uses Draco compression, which is not supported");
            }
            if (prim.targets_count) warn("morph targets are not supported yet");
            MeshData part = convertPrimitive(prim);
            if (part.vertices.empty()) {
                continue;
            }
            const auto base = static_cast<uint32_t>(out.vertices.size());
            Submesh sub;
            sub.firstIndex = static_cast<uint32_t>(out.indices.size());
            sub.indexCount = static_cast<uint32_t>(part.indices.size());
            sub.materialIndex = prim.material ? static_cast<uint32_t>(prim.material - m_data->materials) : UINT32_MAX;
            for (uint32_t index : part.indices) {
                out.indices.push_back(index + base);
            }
            out.vertices.insert(out.vertices.end(), part.vertices.begin(), part.vertices.end());
            out.submeshes.push_back(sub);
        }
        out.computeBounds();
        return out;
    }

    MeshData convertPrimitive(const cgltf_primitive& prim) {
        MeshData part;
        const cgltf_accessor* positions = nullptr;
        const cgltf_accessor* normals = nullptr;
        const cgltf_accessor* tangents = nullptr;
        const cgltf_accessor* uvs = nullptr;
        const cgltf_accessor* colors = nullptr;
        for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
            const cgltf_attribute& attr = prim.attributes[a];
            switch (attr.type) {
            case cgltf_attribute_type_position: positions = attr.data; break;
            case cgltf_attribute_type_normal: normals = attr.data; break;
            case cgltf_attribute_type_tangent: tangents = attr.data; break;
            case cgltf_attribute_type_texcoord: if (attr.index == 0) uvs = attr.data; break;
            case cgltf_attribute_type_color: if (attr.index == 0) colors = attr.data; break;
            default: break;
            }
        }
        if (!positions) {
            warn("primitive without POSITION skipped");
            return part;
        }
        const cgltf_size count = positions->count;
        part.vertices.resize(count);
        for (cgltf_size i = 0; i < count; ++i) {
            Vertex& v = part.vertices[i];
            cgltf_accessor_read_float(positions, i, glm::value_ptr(v.position), 3);
            if (normals) cgltf_accessor_read_float(normals, i, glm::value_ptr(v.normal), 3);
            if (tangents) cgltf_accessor_read_float(tangents, i, glm::value_ptr(v.tangent), 4);
            if (uvs) cgltf_accessor_read_float(uvs, i, glm::value_ptr(v.uv0), 2);
            if (colors) {
                float c[4] = {1, 1, 1, 1};
                cgltf_accessor_read_float(colors, i, c, cgltf_num_components(colors->type));
                v.color = glm::make_vec4(c);
            }
        }
        if (prim.indices) {
            part.indices.resize(prim.indices->count);
            for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                part.indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
            }
        } else {
            part.indices.resize(count);
            for (cgltf_size i = 0; i < count; ++i) part.indices[i] = static_cast<uint32_t>(i);
        }
        part.submeshes = {{0, static_cast<uint32_t>(part.indices.size()), 0, {}}};
        const std::string error = part.validate();
        if (!error.empty()) {
            fail("glTF '" + m_path.string() + "' has an invalid primitive: " + error);
        }
        if (!normals) part.generateNormals();
        if (!tangents) part.generateTangents();
        return part;
    }

    void visit(const cgltf_node* node) {
        if (node->mesh) {
            ModelNode out;
            out.name = node->name ? node->name : "";
            out.mesh = static_cast<int>(node->mesh - m_data->meshes);
            cgltf_node_transform_world(node, glm::value_ptr(out.world));
            m_model.bounds.expand(m_model.meshes[static_cast<size_t>(out.mesh)].bounds.transformed(out.world));
            m_model.nodes.push_back(out);
        }
        for (cgltf_size i = 0; i < node->children_count; ++i) {
            visit(node->children[i]);
        }
    }

    void collectNodes() {
        const cgltf_data* d = m_data.get();
        const cgltf_scene* scene = d->scene ? d->scene : (d->scenes_count ? &d->scenes[0] : nullptr);
        if (scene) {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i) visit(scene->nodes[i]);
        } else {
            for (cgltf_size i = 0; i < d->nodes_count; ++i) {
                if (!d->nodes[i].parent) visit(&d->nodes[i]);
            }
        }
        if (m_model.nodes.empty()) {
            // Files without a node hierarchy: one identity instance per mesh.
            for (size_t i = 0; i < m_model.meshes.size(); ++i) {
                m_model.nodes.push_back({m_model.meshes[i].name, static_cast<int>(i), glm::mat4(1.0f)});
                m_model.bounds.expand(m_model.meshes[i].bounds);
            }
        }
    }

    std::filesystem::path m_path;
    GltfLoadOptions m_options;
    std::unique_ptr<cgltf_data, DataDeleter> m_data;
    ModelData m_model;
    std::map<std::pair<const cgltf_image*, TextureUsage>, int> m_imageIndex;
};

} // namespace

ModelData loadGltf(const std::filesystem::path& path, const GltfLoadOptions& options) {
    ModelData model = Importer(path, options).run();
    const TextureLoadStats& t = model.textureStats;
    if (options.useCompiledTextures && t.source > 0) {
        ZODAR_LOG_INFO("'{}': {} of {} textures loaded from source images{}; run "
                       "'ZodarAssetCompiler model {}' to bake mips and compress them",
                       path.filename().string(), t.source, t.source + t.compiled,
                       t.runtimeMips ? " with runtime mip generation" : "", path.filename().string());
    }
    return model;
}

} // namespace zodar
