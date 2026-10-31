# scene_from_json

`scene.scene.json` (one inline and one file-referenced material, a parent with four children)
is loaded by the GPU test `scene file: JSON scene renders identically to the equivalent C++
scene` (`engine/tests/Gpu/WorldRenderTests.cpp`), which renders it pixel-identical to the same
scene built in C++. Meshes `cube` and `sphere` are bound in code. Schema:
`docs/guide/SCENE_FILES.md`.
