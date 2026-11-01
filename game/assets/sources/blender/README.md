# Blender sources

`<module>.blend` next to each real Blender export in `../../models/<module>.glb`, textures as
separate PNGs in `textures/` (not packed). Regular Git, no LFS: save compressed, at most
20 MiB per file, never commit `*.blend1`/`*.blend2` (ctest `tools.game_source_sizes`).
Export settings: `docs/guide/MODELING.md`. None yet: the airlock arch is a generated
Blender-style placeholder (`game/tools/make_greybox_kit.py`).
