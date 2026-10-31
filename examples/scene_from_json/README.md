# scene_from_json

`scene.scene.json`: one inline and one file-referenced material, a parent with four children,
a lamp prefab instance (`prefabs/lamp.prefab.json`, point light intensity overridden), an
environment (clear colour) and a camera. The GPU test `scene file: JSON scene renders
identically to the equivalent C++ scene` (`engine/tests/Gpu/WorldRenderTests.cpp`) renders it
pixel-identical to the same scene built in C++ (meshes `cube` and `sphere` are bound in code).
Schema: `docs/guide/SCENE_FILES.md`, components: `docs/guide/SCENE_COMPONENTS.md`.
