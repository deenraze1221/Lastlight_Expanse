#include "SceneFile/SceneFile.h"

#include "SceneFile/JsonRead.h"

#include "Renderer/Mesh.h"

#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <sstream>
#include <unordered_set>

namespace zodar {

using detail::child;
using detail::Issues;
using detail::Json;

namespace {

constexpr std::size_t kMaxPrefabDepth = 16;

// Calls f(&SceneEntity::member) for every reflected scene component.
template <typename F>
void forEachReflected(F&& f) {
    f(&SceneEntity::camera);
    f(&SceneEntity::directionalLight);
    f(&SceneEntity::pointLight);
    f(&SceneEntity::spotLight);
    f(&SceneEntity::environment);
    f(&SceneEntity::fog);
}

template <typename Member>
using ComponentOf = typename std::remove_cvref_t<decltype(std::declval<SceneEntity&>().*std::declval<Member>())>::value_type;

// --- Reflected components <-> JSON -----------------------------------------------------

void readReflected(const Json& object, const std::string& path, const ComponentInfo& info, void* component,
                   Issues& issues) {
    if (!detail::requireObject(object, path, issues)) {
        return;
    }
    std::vector<std::string_view> names;
    for (const FieldInfo& field : info.fields) {
        names.push_back(field.name);
    }
    detail::checkKeysIn(object, path, names, issues);
    for (const FieldInfo& f : info.fields) {
        switch (f.type) {
        case FieldType::Bool:
            detail::readBool(object, f.name, path, f.get<bool>(component), issues);
            break;
        case FieldType::Uint:
            detail::readUint(object, f.name, path, f.get<uint32_t>(component), issues);
            break;
        case FieldType::Float:
            detail::readFloat(object, f.name, path, f.get<float>(component), issues, f.min, f.max);
            break;
        case FieldType::Vec3:
            detail::readVec<3>(object, f.name, path, f.get<glm::vec3>(component), issues, f.min, f.max);
            break;
        case FieldType::Vec4:
            detail::readVec<4>(object, f.name, path, f.get<glm::vec4>(component), issues, f.min, f.max);
            break;
        case FieldType::Quat: {
            glm::vec4 q(0.0f);
            if (detail::readVec<4>(object, f.name, path, q, issues)) {
                f.get<glm::quat>(component) = glm::quat(q.w, q.x, q.y, q.z);
            }
            break;
        }
        case FieldType::String:
            detail::readString(object, f.name, path, f.get<std::string>(component), issues);
            break;
        }
    }
}

Json reflectedJson(const ComponentInfo& info, const void* component) {
    Json j = Json::object();
    for (const FieldInfo& f : info.fields) {
        const std::string key(f.name);
        switch (f.type) {
        case FieldType::Bool:
            j[key] = f.get<bool>(component);
            break;
        case FieldType::Uint:
            j[key] = f.get<uint32_t>(component);
            break;
        case FieldType::Float:
            j[key] = f.get<float>(component);
            break;
        case FieldType::Vec3:
            j[key] = detail::vecJson(f.get<glm::vec3>(component));
            break;
        case FieldType::Vec4:
            j[key] = detail::vecJson(f.get<glm::vec4>(component));
            break;
        case FieldType::Quat: {
            const glm::quat& q = f.get<glm::quat>(component);
            j[key] = detail::vecJson(glm::vec4(q.x, q.y, q.z, q.w));
            break;
        }
        case FieldType::String:
            j[key] = f.get<std::string>(component);
            break;
        }
    }
    return j;
}

// --- Built-in components ---------------------------------------------------------------

// Reads into `out` (fields not present keep their current value, so overrides can patch).
void readTransform(const Json& object, const std::string& path, Transform& out, Issues& issues) {
    if (!detail::requireObject(object, path, issues)) {
        return;
    }
    detail::checkKeys(object, path, {"position", "rotation", "rotationDegrees", "scale"}, issues);
    Transform t = out;
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

// `partial`: patch an existing renderer (no required fields).
void readMeshRenderer(const Json& object, const std::string& path, SceneMeshRenderer& out, bool partial,
                      Issues& issues) {
    if (!detail::requireObject(object, path, issues)) {
        return;
    }
    detail::checkKeys(object, path, {"mesh", "submesh", "material", "visible"}, issues);
    SceneMeshRenderer r = out;
    detail::readString(object, "mesh", path, r.mesh, issues, !partial);
    detail::readUint(object, "submesh", path, r.submesh, issues);
    detail::readString(object, "material", path, r.material, issues, !partial);
    detail::readBool(object, "visible", path, r.visible, issues);
    out = std::move(r);
}

void readModelRenderer(const Json& object, const std::string& path, SceneModelRenderer& out, bool partial,
                       Issues& issues) {
    if (!detail::requireObject(object, path, issues)) {
        return;
    }
    detail::checkKeys(object, path, {"model", "visible"}, issues);
    SceneModelRenderer r = out;
    detail::readString(object, "model", path, r.model, issues, !partial);
    detail::readBool(object, "visible", path, r.visible, issues);
    out = std::move(r);
}

std::vector<std::string_view> componentKeys(std::initializer_list<std::string_view> base) {
    std::vector<std::string_view> keys(base);
    forEachReflected([&keys](auto member) { keys.push_back(ComponentReflection<ComponentOf<decltype(member)>>::key); });
    return keys;
}

// Component fields of an entity object (shared by entities and prefab overrides). Existing
// components are patched, missing ones created from defaults.
void readEntityComponents(const Json& object, const std::string& path, SceneEntity& entity,
                          const std::unordered_set<std::string>& materialIds, Issues& issues) {
    if (const auto name = object.find("name"); name != object.end()) {
        if (name->is_string()) {
            entity.name = name->get<std::string>();
        } else {
            issues.add(child(path, "name"), "expected a string, got " + detail::typeName(*name));
        }
    }
    if (const auto t = object.find("transform"); t != object.end()) {
        if (!entity.transform) {
            entity.transform.emplace();
        }
        readTransform(*t, child(path, "transform"), *entity.transform, issues);
    }
    if (const auto r = object.find("meshRenderer"); r != object.end()) {
        const bool partial = entity.meshRenderer.has_value();
        if (!partial) {
            entity.meshRenderer.emplace();
        }
        const std::string at = child(path, "meshRenderer");
        readMeshRenderer(*r, at, *entity.meshRenderer, partial, issues);
        const std::string& material = entity.meshRenderer->material;
        if (!material.empty() && !materialIds.contains(material)) {
            issues.add(child(at, "material"), "unknown material '" + material + "' (not defined in /materials)");
        }
    }
    if (const auto r = object.find("modelRenderer"); r != object.end()) {
        const bool partial = entity.modelRenderer.has_value();
        if (!partial) {
            entity.modelRenderer.emplace();
        }
        readModelRenderer(*r, child(path, "modelRenderer"), *entity.modelRenderer, partial, issues);
    }
    forEachReflected([&](auto member) {
        using T = ComponentOf<decltype(member)>;
        const std::string_view key = ComponentReflection<T>::key;
        if (const auto it = object.find(key); it != object.end()) {
            auto& component = entity.*member;
            if (!component) {
                component.emplace();
            }
            readReflected(*it, child(path, key), componentInfo<T>(), &*component, issues);
        }
    });
}

// --- Parsing -----------------------------------------------------------------------------

struct ParseState {
    std::vector<std::string> prefabStack; // Normalised prefab paths being expanded (cycle check).
    std::vector<std::filesystem::path>* files = nullptr; // Every scene/prefab file read (hot reload).
};

SceneDocument parseSceneImpl(std::string_view json, const std::string& file, const std::filesystem::path& baseDir,
                             ParseState& state);

std::string normalisedKey(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    return (ec ? path.lexically_normal() : canonical).generic_string();
}

// Loads `prefabPath` (relative to baseDir) and appends its entities after the instance entity.
void expandPrefab(std::size_t instance, const std::string& prefabPath, const Json* overrides, const std::string& path,
                  const std::filesystem::path& baseDir, SceneDocument& doc, std::vector<std::string>& sourcePaths,
                  std::unordered_set<std::string>& materialIds, Issues& issues, ParseState& state) {
    const std::filesystem::path full = baseDir / prefabPath;
    const std::string key = normalisedKey(full);
    const std::string at = child(path, "prefab");
    if (std::find(state.prefabStack.begin(), state.prefabStack.end(), key) != state.prefabStack.end()) {
        std::string chain;
        for (const std::string& p : state.prefabStack) {
            chain += p + " -> ";
        }
        issues.add(at, "prefab cycle: " + chain + key);
        return;
    }
    if (state.prefabStack.size() >= kMaxPrefabDepth) {
        issues.add(at, "prefabs nested deeper than " + std::to_string(kMaxPrefabDepth) + " levels");
        return;
    }
    SceneDocument prefab;
    state.prefabStack.push_back(key);
    if (state.files && std::ranges::find(*state.files, std::filesystem::path(key)) == state.files->end()) {
        state.files->emplace_back(key);
    }
    try {
        prefab = parseSceneImpl(detail::readTextFile(full), full.generic_string(), full.parent_path(), state);
    } catch (const DataError& e) {
        state.prefabStack.pop_back();
        issues.add(at, "cannot load prefab '" + prefabPath + "'");
        issues.append(e.issues());
        return;
    }
    state.prefabStack.pop_back();

    // Prefab materials: shared by all instances of the same prefab path.
    const std::string prefix = prefabPath + "#";
    const std::filesystem::path relativeDir = std::filesystem::path(prefabPath).parent_path();
    for (const SceneMaterial& material : prefab.materials) {
        SceneMaterial copy = material;
        copy.id = prefix + material.id;
        if (!copy.source.empty()) {
            copy.source = (relativeDir / material.source).lexically_normal().generic_string();
        }
        if (materialIds.insert(copy.id).second) {
            doc.materials.push_back(std::move(copy));
        }
    }

    const std::string& instanceId = doc.entities[instance].id;
    std::unordered_map<std::string, std::size_t> local;
    std::vector<SceneEntity> copies;
    for (const SceneEntity& entity : prefab.entities) {
        SceneEntity copy = entity;
        if (!entity.id.empty()) {
            copy.id = instanceId + "/" + entity.id;
            local.emplace(entity.id, copies.size());
        }
        copy.parent = entity.parent.empty() ? instanceId : instanceId + "/" + entity.parent;
        if (copy.meshRenderer) {
            copy.meshRenderer->material = prefix + entity.meshRenderer->material;
        }
        if (copy.modelRenderer && isModelFileReference(copy.modelRenderer->model)) {
            // Model files are relative to the prefab: rebase like material files.
            copy.modelRenderer->model = (relativeDir / copy.modelRenderer->model).lexically_normal().generic_string();
        }
        copies.push_back(std::move(copy));
    }

    if (overrides != nullptr) {
        const std::string overridesPath = child(path, "overrides");
        if (detail::requireObject(*overrides, overridesPath, issues)) {
            static const std::vector<std::string_view> allowed = componentKeys({"name", "transform", "meshRenderer", "modelRenderer"});
            for (const auto& [id, value] : overrides->items()) {
                const std::string entryPath = child(overridesPath, id);
                const auto target = local.find(id);
                if (target == local.end()) {
                    issues.add(entryPath, "prefab '" + prefabPath + "' has no entity with id '" + id + "'");
                    continue;
                }
                if (detail::requireObject(value, entryPath, issues)) {
                    detail::checkKeysIn(value, entryPath, allowed, issues);
                    readEntityComponents(value, entryPath, copies[target->second], materialIds, issues);
                }
            }
        }
    }
    for (SceneEntity& copy : copies) {
        doc.entities.push_back(std::move(copy));
        sourcePaths.push_back(at);
    }
}

SceneDocument parseSceneImpl(std::string_view json, const std::string& file, const std::filesystem::path& baseDir,
                             ParseState& state) {
    const Json root = detail::parseJson(json, file);
    const detail::JsonLocator locator(json);
    Issues issues(file, &locator);
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
                    material.data = loadMaterialFile(baseDir / material.source);
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

    std::vector<std::string> sourcePaths; // JSON path of each document entity (errors).
    if (const auto it = root.find("entities"); it != root.end()) {
        if (!it->is_array()) {
            issues.add("/entities", "expected an array, got " + detail::typeName(*it));
        } else {
            static const std::vector<std::string_view> allowed =
                componentKeys({"id", "parent", "name", "transform", "meshRenderer", "modelRenderer", "prefab", "overrides"});
            for (std::size_t i = 0; i < it->size(); ++i) {
                const Json& object = (*it)[i];
                const std::string path = child("/entities", i);
                SceneEntity entity;
                std::string prefab;
                if (detail::requireObject(object, path, issues)) {
                    detail::checkKeysIn(object, path, allowed, issues);
                    detail::readString(object, "id", path, entity.id, issues);
                    detail::readString(object, "parent", path, entity.parent, issues);
                    readEntityComponents(object, path, entity, materialIds, issues);
                    if (detail::readString(object, "prefab", path, prefab, issues) && entity.id.empty()) {
                        issues.add(child(path, "id"), "a prefab instance needs an id (prefab entity ids are prefixed with it)");
                        prefab.clear();
                    }
                    if (object.contains("overrides") && !object.contains("prefab")) {
                        issues.add(child(path, "overrides"), "'overrides' needs 'prefab'");
                    }
                }
                doc.entities.push_back(std::move(entity));
                sourcePaths.push_back(path);
                if (!prefab.empty()) {
                    const auto overrides = object.find("overrides");
                    expandPrefab(doc.entities.size() - 1, prefab, overrides != object.end() ? &*overrides : nullptr,
                                 path, baseDir, doc, sourcePaths, materialIds, issues, state);
                }
            }
        }
    }

    // Ids must be unique (also across expanded prefabs); parents must exist and never cycle.
    std::unordered_map<std::string, std::size_t> entityIds;
    for (std::size_t i = 0; i < doc.entities.size(); ++i) {
        const std::string& id = doc.entities[i].id;
        if (!id.empty() && !entityIds.emplace(id, i).second) {
            issues.add(child(sourcePaths[i], "id"), "duplicate entity id '" + id + "'");
        }
    }
    for (std::size_t i = 0; i < doc.entities.size(); ++i) {
        const SceneEntity& entity = doc.entities[i];
        if (entity.parent.empty()) {
            continue;
        }
        const std::string path = child(sourcePaths[i], "parent");
        const auto parent = entityIds.find(entity.parent);
        if (parent == entityIds.end()) {
            issues.add(path, "unknown parent entity id '" + entity.parent + "'");
            continue;
        }
        std::size_t current = parent->second;
        for (std::size_t steps = 0; steps <= doc.entities.size(); ++steps) {
            if (current == i) {
                issues.add(path, (entity.id.empty() ? "entity " + std::to_string(i) : "entity '" + entity.id + "'") +
                                     " is its own ancestor (parent cycle)");
                break;
            }
            const auto next = entityIds.find(doc.entities[current].parent);
            if (doc.entities[current].parent.empty() || next == entityIds.end()) {
                break;
            }
            current = next->second;
        }
    }

    // Renderers: one kind per entity; model file references must exist (registry ids are
    // resolved when models are loaded).
    for (std::size_t i = 0; i < doc.entities.size(); ++i) {
        const SceneEntity& entity = doc.entities[i];
        if (!entity.modelRenderer) {
            continue;
        }
        const std::string path = child(sourcePaths[i], "modelRenderer");
        if (entity.meshRenderer) {
            issues.add(path, "an entity has either a meshRenderer or a modelRenderer, not both");
        }
        const std::string& model = entity.modelRenderer->model;
        if (!model.empty() && isModelFileReference(model) && !std::filesystem::exists(baseDir / model)) {
            issues.add(child(path, "model"), "model file '" + model + "' not found (relative to the scene file)");
        }
    }

    issues.throwIfAny();
    return doc;
}

} // namespace

SceneDocument parseScene(std::string_view json, std::string_view file, const std::filesystem::path& baseDirectory) {
    ParseState state;
    return parseSceneImpl(json, std::string(file), baseDirectory, state);
}

bool isModelFileReference(std::string_view reference) {
    std::string lower(reference);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.ends_with(".gltf") || lower.ends_with(".glb");
}

std::filesystem::path sceneMaterialDirectory(const SceneMaterial& material, const std::filesystem::path& sceneDirectory) {
    std::filesystem::path directory = sceneDirectory;
    std::string_view id = material.id;
    for (auto pos = id.find('#'); pos != std::string_view::npos; pos = id.find('#')) {
        directory /= std::filesystem::path(std::string(id.substr(0, pos))).parent_path();
        id.remove_prefix(pos + 1);
    }
    return directory.lexically_normal();
}

SceneDocument loadSceneFile(const std::filesystem::path& path, std::vector<std::filesystem::path>* files) {
    ParseState state;
    state.prefabStack.push_back(normalisedKey(path));
    if (files) {
        files->clear();
        files->emplace_back(state.prefabStack.back());
        state.files = files;
    }
    return parseSceneImpl(detail::readTextFile(path), path.generic_string(), path.parent_path(), state);
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
            e["transform"] = reflectedJson(componentInfo<Transform>(), &*entity.transform);
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
        if (entity.modelRenderer) {
            Json renderer = Json::object();
            renderer["model"] = entity.modelRenderer->model;
            if (!entity.modelRenderer->visible) {
                renderer["visible"] = false;
            }
            e["modelRenderer"] = std::move(renderer);
        }
        forEachReflected([&](auto member) {
            using T = ComponentOf<decltype(member)>;
            if (const auto& component = entity.*member) {
                e[std::string(ComponentReflection<T>::key)] = reflectedJson(componentInfo<T>(), &*component);
            }
        });
        entities.push_back(std::move(e));
    }
    root["entities"] = std::move(entities);
    return root.dump(2) + "\n";
}

std::unordered_map<std::string, gfx::MaterialHandle>
createSceneMaterials(const SceneDocument& document, gfx::MaterialSystem& materials, std::string_view file) {
    Issues issues{std::string(file)};
    std::vector<gfx::ShaderHandle> shaders;
    for (const SceneMaterial& material : document.materials) {
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
        if (entity.modelRenderer) {
            const std::string& model = entity.modelRenderer->model;
            const auto bound = bindings.models.find(model);
            if (bound == bindings.models.end() || bound->second == nullptr) {
                issues.add(child(child(path, "modelRenderer"), "model"),
                           "model '" + model + "' is not bound (SceneBindings::models)");
            }
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
        if (entity.modelRenderer) {
            world.add<ModelRenderer>(e, ModelRenderer{bindings.models.at(entity.modelRenderer->model),
                                                      entity.modelRenderer->visible});
        }
        forEachReflected([&](auto member) {
            if (const auto& component = entity.*member) {
                world.add<ComponentOf<decltype(member)>>(e, *component);
            }
        });
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

SceneCaptureNames invertBindings(const SceneDocument& document, const SceneBindings& bindings) {
    SceneCaptureNames names;
    // Several names may bind the same object: keep the lexicographically smallest (deterministic).
    for (const auto& [name, mesh] : bindings.meshes) {
        auto [it, inserted] = names.meshes.emplace(mesh, name);
        if (!inserted && name < it->second) {
            it->second = name;
        }
    }
    for (const auto& [name, handle] : bindings.materials) {
        auto [it, inserted] = names.materials.emplace(handle, name);
        if (!inserted && name < it->second) {
            it->second = name;
        }
    }
    for (const auto& [reference, model] : bindings.models) {
        auto [it, inserted] = names.models.emplace(model, reference);
        if (!inserted && reference < it->second) {
            it->second = reference;
        }
    }
    names.materialDefinitions = document.materials;
    return names;
}

SceneDocument captureScene(const World& world, const SceneCaptureNames& names, std::string_view file) {
    // Roots by entity index, then depth-first in child order: parents precede children and
    // re-instantiation reproduces the child order.
    std::vector<Entity> order;
    const std::function<void(Entity)> visit = [&](Entity e) {
        order.push_back(e);
        for (const Entity c : world.children(e)) {
            visit(c);
        }
    };
    for (const Entity e : world.entities()) {
        if (world.parent(e).isNull()) {
            visit(e);
        }
    }
    std::unordered_map<Entity::Id, std::size_t> index;
    for (std::size_t i = 0; i < order.size(); ++i) {
        index.emplace(order[i].id(), i);
    }

    Issues issues{std::string(file)};
    SceneDocument doc;
    doc.materials = names.materialDefinitions;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Entity e = order[i];
        SceneEntity s;
        if (!world.children(e).empty()) {
            s.id = "e" + std::to_string(i);
        }
        if (const Entity parent = world.parent(e); !parent.isNull()) {
            s.parent = "e" + std::to_string(index.at(parent.id()));
        }
        if (const Name* name = world.tryGet<Name>(e)) {
            s.name = name->value;
        }
        if (const Transform* t = world.tryGet<Transform>(e)) {
            s.transform = *t;
        }
        if (const MeshRenderer* r = world.tryGet<MeshRenderer>(e)) {
            const auto mesh = names.meshes.find(r->mesh);
            const auto material = names.materials.find(r->material);
            const std::string path = child(child("/entities", i), "meshRenderer");
            const std::string label = s.name ? "entity '" + *s.name + "'" : "entity " + std::to_string(i);
            if (mesh == names.meshes.end()) {
                issues.add(child(path, "mesh"), label + ": mesh has no name (SceneCaptureNames::meshes)");
            }
            if (material == names.materials.end()) {
                issues.add(child(path, "material"), label + ": material handle " + std::to_string(r->material) +
                                                        " has no id (SceneCaptureNames::materials)");
            }
            if (mesh != names.meshes.end() && material != names.materials.end()) {
                s.meshRenderer = SceneMeshRenderer{mesh->second, r->submesh, material->second, r->visible};
            }
        }
        if (const ModelRenderer* r = world.tryGet<ModelRenderer>(e)) {
            const auto model = names.models.find(r->model);
            if (model == names.models.end()) {
                issues.add(child(child("/entities", i), "modelRenderer"),
                           (s.name ? "entity '" + *s.name + "'" : "entity " + std::to_string(i)) +
                               ": model has no reference (SceneCaptureNames::models)");
            } else {
                s.modelRenderer = SceneModelRenderer{model->second, r->visible};
            }
        }
        forEachReflected([&](auto member) {
            if (const auto* component = world.tryGet<ComponentOf<decltype(member)>>(e)) {
                s.*member = *component;
            }
        });
        doc.entities.push_back(std::move(s));
    }
    issues.throwIfAny();
    return doc;
}

namespace {

void appendComponentTable(std::ostringstream& out, const ComponentInfo& info, const void* defaults) {
    out << "\n## `" << info.key << "`\n\n" << info.description << "\n\n";
    out << "| Field | Type | Default | Range | Description |\n| --- | --- | --- | --- | --- |\n";
    for (const FieldInfo& f : info.fields) {
        std::ostringstream range;
        const bool hasMin = f.min > std::numeric_limits<float>::lowest();
        const bool hasMax = f.max < std::numeric_limits<float>::max();
        if (hasMin && hasMax) {
            range << f.min << " .. " << f.max;
        } else if (hasMin) {
            range << ">= " << f.min;
        } else if (hasMax) {
            range << "<= " << f.max;
        }
        out << "| `" << f.name << "` | " << fieldTypeName(f.type) << " | `" << formatField(f, defaults) << "` | "
            << range.str() << " | " << f.description << " |\n";
    }
}

} // namespace

std::string sceneComponentReference() {
    std::ostringstream out;
    out << "# Scene file components\n\n"
           "Generated from the component reflection tables by `zodar::sceneComponentReference()`\n"
           "(`engine/rendering/SceneFile`); do not edit by hand. Regenerate with\n"
           "`ZODAR_UPDATE_DOCS=1 ./build/bin/ZodarTests -tc='scene file: reflection*'`.\n"
           "Every field is optional. Vector ranges apply per component. Scene file format:\n"
           "`SCENE_FILES.md`.\n";
    const Transform transform;
    appendComponentTable(out, componentInfo<Transform>(), &transform);
    out << "\nAlso accepted: `rotationDegrees` [3] (Euler XYZ degrees, instead of `rotation`) and a number for a\n"
           "uniform `scale`.\n";
    forEachReflected([&out](auto member) {
        using T = ComponentOf<decltype(member)>;
        const T defaults{};
        appendComponentTable(out, componentInfo<T>(), &defaults);
    });
    return out.str();
}

} // namespace zodar
