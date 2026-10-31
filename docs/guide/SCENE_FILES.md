# Scene and material files (Phase B2)

Module `Zodar::SceneFile` (`engine/rendering/SceneFile`). Headers: `SceneFile/SceneFile.h`,
`SceneFile/MaterialFile.h`, `SceneFile/DataError.h`. Example: `examples/scene_from_json`.

## Loading a scene

```cpp
#include "SceneFile/SceneFile.h"

zodar::SceneDocument doc = zodar::loadSceneFile("level.scene.json"); // parse + validate (CPU)
zodar::SceneBindings bindings;
bindings.meshes["cube"] = &cubeMesh;                                  // GpuMesh* from code
bindings.materials = zodar::createSceneMaterials(doc, sceneRenderer.materials());
zodar::SceneInstance scene = zodar::instantiateScene(doc, world, bindings);
// Render through WorldRender: collectDraws(world, draws) or WorldRenderSync::sync(world).
```

`meshRenderer` meshes are bound by name from code; whole glTF models are referenced with
`modelRenderer` (Phase C4). `serializeScene(doc)`
writes deterministic JSON; `parseScene(serializeScene(doc))` equals `doc` exactly.

Lights, camera, environment and fog: `collectLighting(world, view)` and
`collectCamera(world, view.camera)` (`WorldRender/SceneComponents.h`). Component fields are
generated from the reflection tables into `SCENE_COMPONENTS.md`.

Saving a live World: `captureScene(world, invertBindings(doc, bindings))` returns a document
(roots by entity index, children depth-first; parents get ids `e<n>`), then `serializeScene`.

Mutable materials: `updateMaterial(materialSystem, handle, materialData)` re-applies edited
values (e.g. a reloaded `*.material.json`) to a live handle. The GPU write is deferred to the
next recorded frame and ordered after frames in flight; shader, `alphaMode` and `doubleSided`
cannot change (create a new material).

## `*.scene.json`

```json
{
  "version": 1,
  "materials": {
    "red": "materials/red.material.json",
    "blue": { "baseColor": [0.2, 0.4, 0.9, 1], "roughness": 0.35 }
  },
  "entities": [
    { "id": "root", "name": "Pedestal",
      "transform": { "position": [0, 0.5, -3], "rotation": [0, 0.19866933, 0, 0.98006658], "scale": 1 },
      "meshRenderer": { "mesh": "cube", "material": "red" } },
    { "name": "Orb", "parent": "root", "transform": { "position": [0, 1.2, 0] },
      "meshRenderer": { "mesh": "sphere", "submesh": 0, "material": "blue", "visible": true } }
  ]
}
```

| Field | Type | Notes |
| --- | --- | --- |
| `version` | integer | Optional, must be `1` |
| `materials` | object | id -> inline material object, or path of a `*.material.json` relative to the scene file |
| `entities[]` | array of objects | Created in file order; children keep file order |
| `.id` | string | Optional, unique; needed to be a `parent` |
| `.parent` | string | Entity id (forward references allowed); cycles are errors |
| `.name` | string | `Name` component |
| `.transform` | object | `Transform`: `position` [3], `rotation` quaternion **[x, y, z, w]** (unit length) or `rotationDegrees` [3] (Euler XYZ; saved as quaternion), `scale` [3] or number |
| `.meshRenderer` | object | `mesh` (required, bound name), `material` (required, id in `materials`), `submesh` (default 0), `visible` (default true) |
| `.camera`, `.directionalLight`, `.pointLight`, `.spotLight`, `.environment`, `.fog` | object | Reflected components, see `SCENE_COMPONENTS.md`; direction = entity -Z |
| `.prefab` | string | `*.prefab.json` path relative to this file; the entity needs an `id` |
| `.overrides` | object | Prefab entity id -> fields to patch (`name`, `transform`, `meshRenderer`, components) |

### `modelRenderer` (Phase C4)

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `model` | string | required | `.gltf`/`.glb` path relative to the file (must exist), or an `assets.json` model id |
| `visible` | bool | true | |

Exclusive with `meshRenderer`. Bound through `SceneBindings::models` (by the reference as
written; prefab paths are rebased to the scene). Loading: `AssetManager::loadSceneModels`
(`docs/guide/ASSETS.md`). Overrides may patch it (`{"modelRenderer": {"visible": false}}`).

## Prefabs (`*.prefab.json`)

Same format as a scene. They are expanded while parsing (the document is flat):

```json
{ "id": "lamp", "prefab": "prefabs/lamp.prefab.json", "transform": { "position": [0, 3, 2] },
  "overrides": { "bulb": { "pointLight": { "intensity": 25 } } } }
```

- The instance entity becomes the parent of the prefab's root entities; prefab entities
  follow it in document order.
- Prefab entity ids become `<instance id>/<id>` (`lamp/bulb`); prefab material ids become
  `<prefab path>#<id>`, added once per prefab path; material file paths are rebased to the scene.
- Overrides patch only the given fields (other fields keep the prefab values); material names
  in overrides refer to the including file's materials (or the prefixed prefab ids).
- Nesting is allowed (16 levels); cycles are errors. `serializeScene` writes the flat result.

## `*.material.json`

All fields optional; defaults match `gfx::MaterialDesc`.

| Field | Type | Default | Range |
| --- | --- | --- | --- |
| `name` | string | file name without `.material.json` (inline: material id) | |
| `shader` | string | `zodar/pbr` | registered shader name |
| `alphaMode` | `opaque` / `mask` / `blend` | `opaque` | |
| `alphaCutoff` | number | 0.5 | 0..1 |
| `doubleSided` | bool | false | |
| `baseColor` | [4] linear RGBA | [1, 1, 1, 1] | 0..1 |
| `metallic`, `roughness` | number | 0, 0.5 | 0..1 |
| `normalScale` | number | 1 | finite |
| `occlusionStrength` | number | 1 | 0..1 |
| `emissive` | [3] linear radiance | [0, 0, 0] | >= 0 |
| `custom0`, `custom1` | [4] | zeros | finite (custom shaders) |

| `baseColorTexture`, `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`, `emissiveTexture` | path string or object | none | see below |

Texture slots (Phase C) take a path string or
`{ "path": "...", "filter": "default|pixelArt|nearest|linear", "wrap": "repeat|clamp|mirror" }`
(defaults `default`, `repeat`). Paths are relative to the file that holds the material (the
material file, or the scene file for inline materials). `default` uses the loader's default
sampler (`AssetManagerDesc::defaultSampler`); `pixelArt` = nearest magnification/minification
with linear mip blending. Textures are loaded and bound only through the AssetManager
(`AssetManager::loadMaterial` / `createSceneMaterials`, guide `docs/guide/ASSETS.md`);
`zodar::createMaterial` / `createSceneMaterials` keep neutral textures. Serialization writes
texture slots only when set (untextured materials serialize as before).

## Hot reload

`AssetManager/LiveScene.h` keeps a scene file in sync with a World (reload on change, failures
keep the previous scene): see `docs/guide/ASSETS.md` ("Scene hot reload").
`loadSceneFile(path, &files)` lists the scene and prefab files it read.

## Errors

Every function throws `zodar::DataError`; `issues()` lists every problem found, `what()` prints
one per line:

```
broken.scene.json:3:6: invalid JSON: syntax error while parsing object key - unexpected ','; expected string literal
level.scene.json:12:40: /entities/4/transform/rotation: quaternion [x, y, z, w] must have unit length (length is 2.000000)
level.scene.json:5:9: /materials/stone/roughnes: unknown field 'roughnes'
<scene>: /entities/0/meshRenderer/mesh: mesh 'cube' is not bound (SceneBindings::meshes)
```

File errors carry `line:column` (schema errors: of the offending value, or of the enclosing
object for a missing field) and the JSON pointer of the value. Errors found after parsing
(`instantiateScene`, `captureScene`, `createSceneMaterials`) carry document paths only. Unknown
fields are errors (typo protection). `instantiateScene` validates all bindings first and leaves
the World unchanged on error; `createSceneMaterials` creates nothing if a shader is unknown.
