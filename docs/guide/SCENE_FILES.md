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

Meshes are bound by name from code until the Phase C asset manager. `serializeScene(doc)`
writes deterministic JSON; `parseScene(serializeScene(doc))` equals `doc` exactly.

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

Textures are not supported in files yet (Phase C asset manager); texture fields are reported
as errors.

## Errors

Every function throws `zodar::DataError`; `issues()` lists every problem found, `what()` prints
one per line:

```
broken.scene.json:3:6: invalid JSON: syntax error while parsing object key - unexpected ','; expected string literal
level.scene.json: /entities/4/transform/rotation: quaternion [x, y, z, w] must have unit length (length is 2.000000)
level.scene.json: /materials/stone/roughnes: unknown field 'roughnes'
<scene>: /entities/0/meshRenderer/mesh: mesh 'cube' is not bound (SceneBindings::meshes)
```

Syntax errors carry `line:column`; schema errors carry the JSON pointer of the value. Unknown
fields are errors (typo protection). `instantiateScene` validates all bindings first and leaves
the World unchanged on error; `createSceneMaterials` creates nothing if a shader is unknown.
