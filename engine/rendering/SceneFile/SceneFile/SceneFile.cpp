#include "SceneFile/SceneFile.h"

#include "SceneFile/JsonRead.h"

#include "Renderer/Mesh.h"

#include <glm/gtc/quaternion.hpp>

#include <unordered_set>

namespace zodar {

using detail::child;
using detail::Issues;
using detail::Json;

namespace {

void readTransform(const Json& object, const std::string& path, Transform& out, Issues& issues) {
    if (!detail::requireObject(object, path, issues)) {
        return;
    }
    detail::checkKeys(object, path, {"position", "rotation", "rotationDegrees", "scale"}, issues);
    Transform t;
    detail::readVec<3>(object, "position", path, t.position, issues);
    if (object.contains("rotation") && object.contains("rotationDegrees")) {
        issues.add(child(path, "rotationDegrees"), "use either 'rotation' or 'rotationDegrees', not both");
    }
    glm::vec4 q(0, 0, 0, 1);
    if (detail::readVec<4>(object, "rotation", path, q, issues)) {
        const float length = glm::length(q);
        if (std::abs(length - 1.0f) > 1e-3f) {
            issues.add(child(path, "rotation"),
                       "quaternion [x, y, z, w] must have unit length (length is " + std::to_string(length) + ")");
        }
        t.rotation = glm::quat(q.w, q.x, q.y, q.z);
    }
    glm::vec3 degrees(0.0f);
    if (detail::readVec<3>(object, "rotationDegrees", path, degrees, issues)) {
        t.rotation = glm::quat(glm::radians(degrees)); // Euler XYZ (glm convention), stored as a quaternion.
    }
    if (const auto it = object.find("scale"); it != object.end() && it->is_number()) {
        float uniform = 1.0f;
        if (detail::readFloatValue(*it, child(path, "scale"), uniform, issues)) {
            t.scale = glm::vec3(uniform);
        }
    } else {
        detail::readVec<3>(object, "scale", path, t.scale, issues);
    }
    out = t;
}

void readMeshRenderer(const Json& object, const std::string& path, SceneMeshRenderer& out, Issues& issues) {
    if (!detail::requireObject(object, path, issues)) {
        return;
    }
    detail::checkKeys(object, path, {"mesh", "submesh", "material", "visible"}, issues);
    SceneMeshRenderer r;
    detail::readString(object, "mesh", path, r.mesh, issues, true);
    detail::readUint(object, "submesh", path, r.submesh, issues);
    detail::readString(object, "material", path, r.material, issues, true);
    detail::readBool(object, "visible", path, r.visible, issues);
    out = std::move(r);
}

std::string entityLabel(const SceneEntity& entity, std::size_t index) {
    if (!entity.id.empty()) {
        return "entity '" + entity.id + "'";
    }
    return "entity " + std::to_string(index);
}

} // namespace

SceneDocument parseScene(std::string_view json, std::string_view file, const std::filesystem::path& baseDirectory) {
    const std::string fileName(file);
    const Json root = detail::parseJson(json, fileName);
    Issues issues(fileName);
    SceneDocument doc;
    if (!detail::requireObject(root, "", issues)) {
        issues.throwIfAny();
    }
    detail::checkKeys(root, "", {"version", "materials", "entities"}, issues);

    uint32_t version = SceneDocument::kVersion;
    if (detail::readUint(root, "version", "", version, issues) && version != SceneDocument::kVersion) {
        issues.add("/version", "unsupported scene version " + std::to_string(version) + " (expected " +
                                   std::to_string(SceneDocument::kVersion) + ")");
    }

    std::unordered_set<std::string> materialIds;
    if (const auto it = root.find("materials"); it != root.end() && detail::requireObject(*it, "/materials", issues)) {
        for (const auto& [id, value] : it->items()) {
            const std::string path = child("/materials", id);
            SceneMaterial material;
            material.id = id;
            if (id.empty()) {
                issues.add(path, "material id must not be empty");
                continue;
            }
            if (value.is_string()) {
                material.source = value.get<std::string>();
                try {
                    material.data = loadMaterialFile(baseDirectory / material.source);
                } catch (const DataError& e) {
                    issues.add(path, "cannot load material file '" + material.source + "'");
                    issues.append(e.issues());
                }
            } else if (value.is_object()) {
                detail::readMaterialObject(value, path, id, material.data, issues);
            } else {
                issues.add(path, "expected a material object or a material file path, got " + detail::typeName(value));
                continue;
            }
            materialIds.insert(id);
            doc.materials.push_back(std::move(material));
        }
    }

    std::unordered_map<std::string, std::size_t> entityIds;
    if (const auto it = root.find("entities"); it != root.end()) {
        if (!it->is_array()) {
            issues.add("/entities", "expected an array, got " + detail::typeName(*it));
        } else {
            for (std::size_t i = 0; i < it->size(); ++i) {
                const Json& object = (*it)[i];
                const std::string path = child("/entities", i);
                SceneEntity entity;
                if (detail::requireObject(object, path, issues)) {
                    detail::checkKeys(object, path, {"id", "name", "parent", "transform", "meshRenderer"}, issues);
                    if (detail::readString(object, "id", path, entity.id, issues) &&
                        !entityIds.emplace(entity.id, i).second) {
                        issues.add(child(path, "id"), "duplicate entity id '" + entity.id + "'");
                    }
                    detail::readString(object, "parent", path, entity.parent, issues);
                    if (const auto name = object.find("name"); name != object.end()) {
                        if (name->is_string()) {
                            entity.name = name->get<std::string>();
                        } else {
                            issues.add(child(path, "name"), "expected a string, got " + detail::typeName(*name));
                        }
                    }
                    if (const auto t = object.find("transform"); t != object.end()) {
                        entity.transform.emplace();
                        readTransform(*t, child(path, "transform"), *entity.transform, issues);
                    }
                    if (const auto r = object.find("meshRenderer"); r != object.end()) {
                        entity.meshRenderer.emplace();
                        readMeshRenderer(*r, child(path, "meshRenderer"), *entity.meshRenderer, issues);
                        const std::string& material = entity.meshRenderer->material;
                        if (!material.empty() && !materialIds.contains(material)) {
                            issues.add(child(child(path, "meshRenderer"), "material"),
                                       "unknown material '" + material + "' (not defined in /materials)");
                        }
                    }
                }
                doc.entities.push_back(std::move(entity));
            }
        }
    }

    // Parent references: must exist, and following parents must never return to the entity.
    for (std::size_t i = 0; i < doc.entities.size(); ++i) {
        const SceneEntity& entity = doc.entities[i];
        if (entity.parent.empty()) {
            continue;
        }
        const std::string path = child(child("/entities", i), "parent");
        const auto parent = entityIds.find(entity.parent);
        if (parent == entityIds.end()) {
            issues.add(path, "unknown parent entity id '" + entity.parent + "'");
            continue;
        }
        std::size_t current = parent->second;
        for (std::size_t steps = 0; steps <= doc.entities.size(); ++steps) {
            if (current == i) {
                issues.add(path, entityLabel(entity, i) + " is its own ancestor (parent cycle)");
                break;
            }
            const auto next = entityIds.find(doc.entities[current].parent);
            if (doc.entities[current].parent.empty() || next == entityIds.end()) {
                break;
            }
            current = next->second;
        }
    }

    issues.throwIfAny();
    return doc;
}

SceneDocument loadSceneFile(const std::filesystem::path& path) {
    return parseScene(detail::readTextFile(path), path.generic_string(), path.parent_path());
}

std::string serializeScene(const SceneDocument& document) {
    Json root = Json::object();
    root["version"] = SceneDocument::kVersion;
    Json materials = Json::object();
    for (const SceneMaterial& material : document.materials) {
        materials[material.id] = material.source.empty() ? detail::materialJson(material.data) : Json(material.source);
    }
    root["materials"] = std::move(materials);
    Json entities = Json::array();
    for (const SceneEntity& entity : document.entities) {
        Json e = Json::object();
        if (!entity.id.empty()) {
            e["id"] = entity.id;
        }
        if (entity.name) {
            e["name"] = *entity.name;
        }
        if (!entity.parent.empty()) {
            e["parent"] = entity.parent;
        }
        if (entity.transform) {
            const Transform& t = *entity.transform;
            Json transform = Json::object();
            transform["position"] = detail::vecJson(t.position);
            transform["rotation"] = detail::vecJson(glm::vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w));
            transform["scale"] = detail::vecJson(t.scale);
            e["transform"] = std::move(transform);
        }
        if (entity.meshRenderer) {
            const SceneMeshRenderer& r = *entity.meshRenderer;
            Json renderer = Json::object();
            renderer["mesh"] = r.mesh;
            if (r.submesh != 0) {
                renderer["submesh"] = r.submesh;
            }
            renderer["material"] = r.material;
            if (!r.visible) {
                renderer["visible"] = false;
            }
            e["meshRenderer"] = std::move(renderer);
        }
        entities.push_back(std::move(e));
    }
    root["entities"] = std::move(entities);
    return root.dump(2) + "\n";
}

std::unordered_map<std::string, gfx::MaterialHandle>
createSceneMaterials(const SceneDocument& document, gfx::MaterialSystem& materials, std::string_view file) {
    Issues issues{std::string(file)};
    std::vector<gfx::ShaderHandle> shaders;
    for (std::size_t i = 0; i < document.materials.size(); ++i) {
        const SceneMaterial& material = document.materials[i];
        try {
            shaders.push_back(materials.findShader(material.data.shader));
        } catch (const Error&) {
            issues.add(child(child("/materials", material.id), "shader"),
                       "unknown shader '" + material.data.shader + "'");
        }
    }
    issues.throwIfAny();
    std::unordered_map<std::string, gfx::MaterialHandle> handles;
    for (std::size_t i = 0; i < document.materials.size(); ++i) {
        handles[document.materials[i].id] = materials.createMaterial(toMaterialDesc(document.materials[i].data, shaders[i]));
    }
    return handles;
}

SceneInstance instantiateScene(const SceneDocument& document, World& world, const SceneBindings& bindings,
                               std::string_view file) {
    Issues issues{std::string(file)};
    std::unordered_map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < document.entities.size(); ++i) {
        const SceneEntity& entity = document.entities[i];
        const std::string path = child("/entities", i);
        if (!entity.id.empty() && !ids.emplace(entity.id, i).second) {
            issues.add(child(path, "id"), "duplicate entity id '" + entity.id + "'");
        }
        if (!entity.meshRenderer) {
            continue;
        }
        const SceneMeshRenderer& r = *entity.meshRenderer;
        const std::string at = child(path, "meshRenderer");
        const auto mesh = bindings.meshes.find(r.mesh);
        if (mesh == bindings.meshes.end() || mesh->second == nullptr) {
            issues.add(child(at, "mesh"), "mesh '" + r.mesh + "' is not bound (SceneBindings::meshes)");
        } else if (r.submesh >= mesh->second->submeshes().size()) {
            issues.add(child(at, "submesh"), "submesh " + std::to_string(r.submesh) + " out of range (mesh '" +
                                                 r.mesh + "' has " +
                                                 std::to_string(mesh->second->submeshes().size()) + ")");
        }
        if (!bindings.materials.contains(r.material)) {
            issues.add(child(at, "material"), "material '" + r.material + "' is not bound (SceneBindings::materials)");
        }
    }
    for (std::size_t i = 0; i < document.entities.size(); ++i) {
        const std::string& parent = document.entities[i].parent;
        if (!parent.empty() && !ids.contains(parent)) {
            issues.add(child(child("/entities", i), "parent"), "unknown parent entity id '" + parent + "'");
        }
    }
    issues.throwIfAny();

    SceneInstance instance;
    instance.entities.reserve(document.entities.size());
    for (const SceneEntity& entity : document.entities) {
        const Entity e = world.create();
        if (entity.name) {
            world.add<Name>(e, Name{*entity.name});
        }
        if (entity.transform) {
            world.add<Transform>(e, *entity.transform);
        }
        if (entity.meshRenderer) {
            const SceneMeshRenderer& r = *entity.meshRenderer;
            world.add<MeshRenderer>(e, MeshRenderer{bindings.meshes.at(r.mesh), r.submesh,
                                                    bindings.materials.at(r.material), r.visible});
        }
        if (!entity.id.empty()) {
            instance.byId.emplace(entity.id, e);
        }
        instance.entities.push_back(e);
    }
    for (std::size_t i = 0; i < document.entities.size(); ++i) {
        const std::string& parent = document.entities[i].parent;
        if (!parent.empty()) {
            // Cycles are rejected by parseScene; a hand-built document with a cycle is rolled
            // back here so the world stays unchanged.
            if (!world.setParent(instance.entities[i], instance.byId.at(parent))) {
                for (const Entity e : instance.entities) {
                    world.destroy(e);
                }
                issues.add(child(child("/entities", i), "parent"), "parent link to '" + parent + "' creates a cycle");
                issues.throwIfAny();
            }
        }
    }
    return instance;
}

} // namespace zodar
