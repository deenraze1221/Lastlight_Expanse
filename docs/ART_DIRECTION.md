# Art Direction — Lastlight Expanse and ZODAR games

**Status:** official direction, adopted 2026-10-31. Documentation only. No Blockbench integration
or renderer change has been made for it yet. The "Future technical requirements" section lists
that work. Schedule it through `PROGRESS.md` / `docs/AI_FIRST_ROADMAP.md`, not ad hoc.

This direction applies to Lastlight Expanse. It is the default for future ZODAR games unless a game
documents its own direction. Engine code stays style-neutral: it provides capabilities (sampling
modes, mip control, lighting, shadows, fog, weather). Games choose the look (see
`docs/ARCHITECTURE.md`, "Rendering: capabilities, not styles").

## 1. Visual style

- Minecraft-inspired voxel / pixel-art aesthetic.
- Crisp, readable pixel textures. No unwanted blur: no bilinear magnification on pixel art and no
  texture compression that smears art pixels.
- Modern lighting on top of the retro surfaces: dynamic shadows, ambient occlusion, fog and weather.
- Low-poly and voxel-like geometry with detailed, readable silhouettes. Detail comes from shape and
  texture, not from dense meshes.

## 2. Texture standards

| Size | Use |
| --- | --- |
| 256×256 | Standard: environment objects, terrain blocks, props, items |
| 512×512 | Important assets: weapons, characters, mutants, hero blocks |
| 1024×1024 | Only when genuinely necessary; give the reason in the asset's notes |

- Sizes are powers of two and square. Non-square atlases need a documented reason.
- **Consistent palette.** Each game keeps one master palette. All textures are quantised to it, or
  to documented sub-palettes per biome or faction.
- **Consistent texel density.** One art pixel should cover about the same world size on every
  asset. The terrain reference is 256² per 1 m voxel face with 2×2-texel art pixels, so one art
  pixel is 1/128 m. Props and characters stay within 2× of that art-pixel size. Hero assets may
  be up to 2× finer. Confirm or adjust this reference when the first Blockbench assets are made.
- **Consistent pixel-art appearance.** Hard edges, deliberate dithering, no photo sources, no
  upscaled low-res art without cleanup.
- **Sampling.** Use nearest-neighbour magnification and minification on pixel art.
- **Mipmaps.** Configure them carefully: blend between mip levels (linear mip filter) to stop
  distant shimmer, build mips with a filter that keeps the palette readable, and set the anisotropy
  per material. Smooth or non-pixel-art textures (UI gradients, sky, effects) may use linear
  filtering.
- Emissive pixels and transparency follow the conventions of the material that uses them. The
  current terrain convention: alpha < 1 marks emissive pixels (`game/shaders/voxel.frag`).

## 3. 3D asset workflow

- **Blockbench** is the primary tool for voxel-style models (props, items, weapons, characters,
  mutants).
- **Exchange format: glTF 2.0** (`.glb` preferred, `.gltf` + files accepted), exported from
  Blockbench. The asset carries meshes, materials, UVs and textures. Later it can also carry
  skins and animations (Phase E).
- **Engine/game split:**
  - ZODAR Engine keeps the asset pipeline generic: glTF import, texture compilation, sampler and
    mip rules, validation and reports. Nothing in `engine/` knows about Blockbench project files
    or Lastlight content.
  - Lastlight Expanse keeps its game-specific assets, palettes, texel-density targets and content
    rules in `game/` (for example a future `game/assets/`).
- **Pipeline:** Blockbench → export `.glb` → place it in the game's asset folder → the engine
  asset compiler (`ZodarAssetCompiler`) validates it and compiles the textures → the game loads
  it through the engine importer. Blockbench `.bbmodel` source files are versioned next to the
  exports so assets can be re-exported.

## 4. Future technical requirements (not implemented)

What exists today:
- `GltfLoader` imports meshes, materials, UVs, textures and glTF samplers. Nearest
  mag/min/mip filters and wrap modes are read into `ModelSampler`. `KHR_texture_transform` is
  accepted.
- `TextureArray` and `zodarSampleArray` exist, with point-filtered sampling and linear mips used
  by the terrain.
- Fog (`SceneLighting::fog`) exists.

Required work, to be scheduled:

1. **glTF import for Blockbench exports** (engine, generic). Verify that glTF sampler filters are
   honoured end to end (renderer samplers, not only `ModelSampler`). Support per-face UVs and
   multiple textures per model. Handle Blockbench's material and texture naming. Report
   unsupported features in the import log.
2. **Pixel-art texture rules in the asset pipeline** (engine, generic, configured per game).
   Each texture gets a "pixel art" usage flag that means: no BCn compression (or an
   art-preserving mode, decided by A/B tests), mip generation that keeps the palette readable,
   nearest sampling, and a max-anisotropy setting. Size validation (256/512/1024, power of two)
   gives a warning, not an error.
3. **Asset validation report** (engine tool). Texture size class, texel density per mesh
   (texels per metre, from UV area against world area), palette deviation against a
   game-supplied palette, and triangle counts. Output is JSON, so agents can check assets
   without looking at them.
4. **Lighting features** (rendering roadmap): S5 dynamic shadows (cascaded sun shadows in
   `pbr.frag` and `voxel.frag`), ambient occlusion beyond the mesher's voxel AO (S7), and
   weather (rain/snow) plus the day–night cycle (backlog).
5. **Game content layout** (Lastlight): `game/assets/` holds `.bbmodel` sources, `.glb`
   exports, the master palette and an asset manifest (Phase C asset registry).
6. **Animation** (Phase E): Blockbench bone animations exported as glTF skins/clips, played by
   the `Animator` component.

## 5. Review checklist for new art

- [ ] Size class correct (256 / 512 / 1024 with reason).
- [ ] Palette matches the game's master palette.
- [ ] Texel density within the documented range.
- [ ] Reads crisply up close (nearest sampling) and does not shimmer at distance (mips).
- [ ] Silhouette readable at gameplay distance.
- [ ] Blockbench source (`.bbmodel`) committed next to the `.glb` export.
