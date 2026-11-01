# Real exports (Phase D1 follow-up)

Put one small real Blockbench export and one small real Blender export here, named
`blockbench_<name>.glb` and `blender_<name>.glb` (export settings: `docs/guide/MODELING.md`).
The unit case "gltf tools: real exports load" checks every `.glb`/`.gltf` in this folder:
it loads, reports the matching source tool, has no importer warnings and plausible bounds.
Then run `python3 tools/zodar.py import engine/tests/data/gltf_tools/real` and keep
`assets check --strict` on `../assets.json` passing.
