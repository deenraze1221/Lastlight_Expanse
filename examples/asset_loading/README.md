# asset_loading

Phase C1 example for `zodar::AssetManager` (guide: `docs/guide/ASSETS.md`).

- `crate.gltf`: glTF 2.0 cube (embedded buffer) with `textures/crate.png` (64×64, primary size)
  and a Blockbench-style nearest sampler.
- `materials/rune.material.json`: base colour + emissive from `textures/rune.png` (32×32, small
  prop) with the `pixelArt` filter.
- `asset_loading.scene.json`: the rune material (file) and an inline `crateLinear` material
  (same crate PNG, `linear` filter) on two cubes, sun, sky, camera.

Run (renders the glTF crate between the pixel-art and the linear cube):

```bash
./build/bin/ZodarAssetLoading --headless --deterministic --frames 30 --screenshot asset_loading.png
```

`make_assets.py` regenerates the PNGs and the glTF (stdlib only, deterministic).
GPU tests: `engine/tests/Gpu/AssetManagerTests.cpp`.
