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

Updated 2026-10-31 (supersedes the earlier 64/128/256 table). Enforced as warnings by the
engine (`Assets/TextureStandards.h`: AssetManager, `zodar.py assets check`); textures are never
resized automatically, violations are reported.

| Size | Use |
| --- | --- |
| 32×32 | **Default** for every asset: terrain blocks, props, items, characters, weapons |
| 64×64 | **Absolute maximum** for ALL assets (characters, weapons and unique assets included) |
| 8×8, 16×16 | Allowed for tiny details |

- Sizes are powers of two and square. Anything above 64×64, non-square or not a power of two is
  a violation (warning: re-author the texture; nothing is resized).
- **Consistent palette.** Each game keeps one master palette. All textures are quantised to it, or
  to documented sub-palettes per biome or faction. Declare them in the game's `assets.json`
  (`"palette"`, `"subPalettes"`); `zodar.py assets check` reports off-palette texels.
- **Consistent texel density.** One art pixel should cover about the same world size on every
  asset. Reference: 32² per 1 m voxel face, one texel per art pixel, so one art pixel is 1/32 m
  (2 texels per Blockbench unit at 16 units/m). Assets stay within 2× of that (16..64
  texels/m; `zodar.py assets check` reports models outside).
- **Consistent pixel-art appearance.** Hard edges, deliberate dithering, no photo sources, no
  upscaled low-res art without cleanup.
- **Sampling.** Use nearest-neighbour magnification and minification on pixel art
  (`gfx::SamplerDesc::pixelArt()`; material files: `"filter": "pixelArt"`).
- **Compression.** Pixel-art textures are never block-compressed: BC7 (and every BCn format) is
  lossy and changes art colours. Compiled pixel-art textures are uncompressed RGBA8 KTX2 (level 0
  byte-identical to the source); the loader rejects a BCn cache for pixel-art textures and
  decodes the PNG instead. Pixel art = glTF sampler with NEAREST magnification (Blockbench), a
  pixelArt/nearest model sampling setting, or `--pixel-art`. BC7 stays available for smooth,
  non-pixel-art textures.
- **Mipmaps.** Configure them carefully: blend between mip levels (linear mip filter) to stop
  distant shimmer, build mips with a filter that keeps the palette readable, and set the anisotropy
  per material. Smooth or non-pixel-art textures (UI gradients, sky, effects) may use linear
  filtering.
- Emissive pixels and transparency follow the conventions of the material that uses them. The
  current terrain convention: alpha < 1 marks emissive pixels (`game/shaders/voxel.frag`).

**Known violations (reported, not resized):** the procedural terrain textures of Lastlight
Expanse (`game/src/Rendering/BlockTextures.h`: 10 standard layers at 256×256 and 1 hero layer at
512×512, 2×2 / 4×4-texel art pixels, i.e. 128×128 art pixels) predate this standard. They are
generated in code, not loaded through the asset pipeline. Bringing them to 32×32 (or 64×64) is
game art work, to be scheduled separately.

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

## 4. Technical requirements

What exists today (Phase C1 added the asset manager, see `docs/guide/ASSETS.md`):
- `AssetManager` loads PNG textures, glTF models and material files with texture slots;
  `pixelArt` filter preset; texture size warnings (32 default, 64 maximum); lossless pixel-art
  compilation (RGBA8 KTX2, BCn rejected).
- glTF sampler filters are honoured end to end (verified by GPU test on the material sampler).
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
   nearest sampling, and a max-anisotropy setting. Size validation (§2: 8/16/32/64) gives a
   warning, not an error (done at load time in Phase C1; the offline compiler check remains).
3. **Asset validation report** (engine tool). Texture size class, texel density per mesh
   (texels per metre, from UV area against world area), palette deviation against a
   game-supplied palette, and triangle counts. Output is JSON, so agents can check assets
   without looking at them. *Done in Phases C3 and C7* (`zodar.py assets check --report`: size
   class, texel density per model, triangle counts, palette deviation against the master palette
   and sub-palettes in `assets.json`).
4. **Lighting features** (rendering roadmap): S5 dynamic shadows (cascaded sun shadows in
   `pbr.frag` and `voxel.frag`), ambient occlusion beyond the mesher's voxel AO (S7), and
   weather (rain/snow) plus the day–night cycle (backlog).
5. **Game content layout** (Lastlight): `game/assets/` holds `.bbmodel` sources, `.glb`
   exports, the master palette and an asset manifest (Phase C asset registry).
6. **Animation** (Phase E): Blockbench bone animations exported as glTF skins/clips, played by
   the `Animator` component.

## 5. Review checklist for new art

- [ ] Size: 32×32 default, never above 64×64 (`zodar.py assets check` reports violations).
- [ ] Compiled losslessly (no BC7 on pixel art).
- [ ] Palette matches the game's master palette.
- [ ] Texel density within the documented range.
- [ ] Reads crisply up close (nearest sampling) and does not shimmer at distance (mips).
- [ ] Silhouette readable at gameplay distance.
- [ ] Blockbench source (`.bbmodel`) committed next to the `.glb` export.
