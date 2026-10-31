# asset_loading

Phase C1 example for `zodar::AssetManager` (guide: `docs/guide/ASSETS.md`).

- `crate.gltf`: glTF 2.0 cube (embedded buffer) with `textures/crate.png` (32×32, the default size)
  and a Blockbench-style nearest sampler.
- `materials/rune.material.json`: base colour + emissive from `textures/rune.png` (32×32) with the `pixelArt` filter.
- `asset_loading.scene.json`: the rune material (file) and an inline `crateLinear` material
  (same crate PNG, `linear` filter) on two cubes; two `modelRenderer` crates (Phase C4): one by
  registry id (`"crate"`), a small one by path (`"crate.gltf"`); sun, sky, camera.

Run (renders the glTF crate between the pixel-art and the linear cube; assets load
asynchronously, `--deterministic` waits for them before the first frame):

```bash
./build/bin/ZodarAssetLoading --headless --deterministic --frames 30 --screenshot asset_loading.png
```

- `assets.json`: registry of all five assets (ids `crate`, `crate_texture`, `rune`,
  `rune_texture`, `asset_loading`); the sample loads the crate by id. ctest
  `tools.asset_check_example` fails when it is stale: after editing an asset run
  `python3 tools/zodar.py import <file>` (from this folder, or pass `--registry`).

- `palette.png`: master palette swatch (6 colours); `assets.json` adds the sub-palette `glow`
  (`#5adcff`) for the rune assets. `zodar.py assets check` reports 0 off-palette texels.

`make_assets.py` regenerates the PNGs and the glTF (stdlib only, deterministic).
GPU tests: `engine/tests/Gpu/AssetManagerTests.cpp`.
