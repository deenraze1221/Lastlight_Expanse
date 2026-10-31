# Building Lastlight Expanse (ZODAR Engine + game)

## Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| CMake | ≥ 3.25 | Verified with 3.28.3 |
| Ninja | any recent | Primary generator |
| C++ compiler | C++20 | Verified: GCC 13.3, Clang (Ubuntu 24.04). MSVC 2022 17.8+ expected, **not yet verified** |
| Vulkan driver | 1.3+ | Any desktop GPU driver from 2022 onwards |
| Vulkan SDK | 1.3.2xx+ | Windows: needed for `glslc` and validation layers. Linux: distro packages are enough |
| Git | any | Dependencies are fetched at configure time |

No Vulkan SDK library is linked; the loader is resolved at runtime.

## Pinned dependencies (fetched by `cmake/ZodarDependencies.cmake`)

| Library | Tag | Commit |
| --- | --- | --- |
| SDL3 | release-3.2.24 | a8589a84226a |
| Vulkan-Headers | v1.4.304 | d4a196d8c84e |
| volk | 1.4.304 | 0b17a763ba56 |
| VulkanMemoryAllocator | v3.3.0 | 1d8f600fd424 |
| spdlog | v1.15.3 | 6fa36017cfd5 |
| GLM | 1.0.1 | 0af55ccecd98 |
| stb (stb_image; stb_image_write in tests) | — | 003501f6d7a8 |
| cgltf | v1.15 | bbeb5b0b070d |
| Dear ImGui (+ SDL3 / Vulkan backends) | v1.92.4 | 9a5d5c45f54b |
| doctest (tests only) | v2.4.12 | 1da23a3e8119 |
| bcdec (BCn decode: runtime fallback, tests) | — | 80859ed3b7af |
| bc7enc_rdo (rgbcx BC1–5 + bc7enc BC7; AssetPipeline only) | — | b9438627eef7 |
| nanosvg (UI: SVG parse + rasterize) | — | 239e102ec2c6 |

stb_truetype (UI fonts) comes from the existing `stb` pin. UI fonts are committed under
`engine/ui/Resources/UI/Fonts` with their OFL licences: Michroma Regular and Rajdhani Light/Regular/Bold,
taken from [google/fonts](https://github.com/google/fonts) at commit `6c1dbe27dcf7`.
`engine/ui/Resources/UI/MANIFEST.sha256` pins every font and original logo file; the unit tests fail if
any of them changes. Resources are copied to `bin/ui` at build time.

Headers are 1.4.304 because volk's tags follow header versions; the engine requests and
requires **API 1.3** plus the baseline in docs/ARCHITECTURE.md (descriptor indexing, timeline semaphores, buffer device address, indirect count). KTX2 is implemented in the engine (S3); later stages add meshoptimizer, FSR, Jolt, miniaudio and ufbx.

## Windows 10/11

1. Install Visual Studio 2022 (Desktop C++), CMake, Ninja, Git and the
   [LunarG Vulkan SDK](https://vulkan.lunarg.com/) (sets `VULKAN_SDK`).
2. Open **x64 Native Tools Command Prompt for VS 2022**:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DZODAR_GPU_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
build\bin\HelloTriangle.exe
```

Expected: a 1280×720 window with a rotating RGB triangle; the log shows your GPU, the swapchain
and `validation: 0 errors, 0 warnings` on exit. Esc or closing the window quits.

## Linux (Ubuntu 24.04)

```bash
sudo apt install build-essential cmake ninja-build git glslang-tools \
  libvulkan-dev vulkan-validationlayers mesa-vulkan-drivers \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DZODAR_GPU_TESTS=ON
```

```bash
cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

```bash
./build/bin/HelloTriangle
```

Wayland is supported by SDL when `libwayland-dev`/`libxkbcommon-dev` are installed at configure time.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `ZODAR_BUILD_SAMPLES` | ON | Build `engine/samples/` |
| `LASTLIGHT_BUILD_GAME` | ON | Build the `LastlightExpanse` game (`game/`) |
| `ZODAR_BUILD_TESTS` | ON | Build `ZodarTests` (doctest) |
| `ZODAR_BUILD_TOOLS` | ON | Build `engine/tools/` (ZodarBench, ZodarAssetCompiler, ZodarUISandbox) |
| `ZODAR_GPU_TESTS` | OFF | Register GPU tests: `gpu.subsystems` (headless, synchronization validation), `gpu.hello_triangle`, `gpu.model_viewer_demo`, `gpu.zodarbench_smoke` (need a video driver) |
| `ZODAR_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` for engine code |

## HelloTriangle arguments

```
--frames N            Render N frames then exit (0 = until closed)
--screenshot F.ppm    Save the last frame (requires --frames)
--no-validation       Do not enable VK_LAYER_KHRONOS_validation
--present MODE        vsync (default) | low-latency | immediate
--width W --height H  Initial window size
```

Exit codes: 0 ok, 1 fatal error, 2 validation errors occurred, 3 screenshot failed.

## ModelViewer

```
ModelViewer [file.gltf|file.glb] [--frames N] [--screenshot F.ppm] [--no-validation]
            [--width W] [--height H] [--yaw DEG] [--pitch DEG] [--zoom F] [--spin DEG_PER_S] [--nearest]
            [--environment sky|none|file.hdr] [--lighting-quality low|medium|high|ultra]
            [--debug-view NAME] [--lights N] [--no-prepass]
```

Without a file it shows a procedural material demo. The environment (default: a procedural sky)
provides image-based lighting and the background; `--lights N` adds N moving coloured point
lights. Left-drag orbits, wheel zooms, F1 toggles the profiler overlay, F2 cycles the lighting
debug views, Esc quits. `--reference-path` uses the pre-render-graph path,
`--no-overlay` hides the profiler.

## ZodarBench

Deterministic benchmark (fixed camera orbit) writing a JSON report: CPU/GPU frame times
(avg/p50/p95/p99/max), per-pass GPU times, CPU scopes, draw calls, visible/culled objects,
triangles, GPU memory and validation counts.

```
ZodarBench [--scene grid|unique|city|model] [--model file.glb] [--objects N] [--blocks N] [--lights N]
           [--width W] [--height H] [--warmup N] [--frames N] [--path gpu|graph|reference]
           [--present immediate|low-latency|vsync] [--overlay] [--no-validation] [--sync-validation]
           [--output report.json] [--screenshot out.ppm] [--screenshot-frame N] [--per-frame]
           [--frame-cap FPS]
```

| Scene | Content | Measures |
| --- | --- | --- |
| `grid` (default) | N objects from 2 meshes x 16 materials, orbit camera | GPU throughput with few (instanced) draw calls |
| `unique` | N objects, each with its own deformed mesh and material, orbit camera | CPU cost per draw call / state change |
| `city` | B x B blocks: 4 buildings + 16 props per block, street-level drive | Culling and overdraw: most objects off-screen or hidden |
| `model` | A glTF file | Real assets |

`--path gpu` (default) renders through a persistent `GpuScene` (GPU culling + indirect
draws); `graph` uses the CPU draw list on the render graph (S1 path); `reference` the
pre-graph path. All three render identical images. GPU-driven statistics arrive
`statsLatencyFrames` frames later; ZodarBench credits them to the frame they describe and
renders a few untimed extra frames so every path reports statistics for the same measured
frames. `commandDraws` counts recorded vkCmdDraw* calls (a multi-draw indirect call counts
once). `--per-frame` adds the per-frame [frame, visible, draws, triangles] series.

`perFrame` reports draw calls, visible/culled objects and triangles averaged over the measured
frames (the view changes along the path); `lastFrame` keeps the final frame's values. The
screenshot is taken in the middle of the measured path.

`--frame-cap FPS` paces frames to a fixed rate (`SDL_DelayPrecise` after present; the wait is
reported as `frameCap.waitMs` and is not part of the CPU frame time). Use the same cap for two
paths to compare GPU times without either one saturating the GPU (clock and queueing effects).
`pipelineStatistics` reports scene-pass fragment shader invocations (total and per pixel) and
rasterized primitives when the device supports `pipelineStatisticsQuery`
(`"supported": false` otherwise). The `reference` path does not record statistics.

Lighting (S4): `--scene lights` (grid with `--lights N`, default 4096 moving point/spot
lights), `--lighting-quality low|medium|high|ultra` (cluster presets), `--brute-force-lights`
(every light for every fragment: A/B against clustering), `--no-prepass`, `--no-multiscatter`,
`--environment none|sky|file.hdr` (IBL + background; default none) and `--debug-view NAME`
(none, light-count, cluster-slice, normals, direct, indirect, specular-ibl). The report's
`lighting` section has the cluster grid, list capacity, assignments, the longest list,
overflowing clusters, IBL bake time/memory and cluster buffer memory. GPU passes are timed
individually (`Lighting.ClusterCull`, `Scene.DepthPrepass`, ...).

`--source-textures` ignores compiled KTX2 textures (PNG/JPEG decode + runtime mips) and
`--no-transfer-queue` uploads on the graphics queue. The `assets` section reports the upload
queue used, `loadMs` (parse + decode/KTX2 read), `stageMs`, `transferMs` (blocking upload),
staged/texture MiB and how many textures came from the compiled cache.

## ZodarAssetCompiler

Offline texture compilation: baked mips + BCn + KTX2.

```
ZodarAssetCompiler model <model.gltf|glb> [--quality fast|normal|high] [--threads N] [--force] [--report r.json]
ZodarAssetCompiler texture <in.png|jpg|tga|bmp> <out.ktx2> [--format bc7|bc1|bc3|bc4|bc5|rgba8]
                   [--srgb|--linear] [--normal] [--no-mips] [--quality ...] [--threads N]
ZodarAssetCompiler info <file.ktx2>
```

`model` writes every texture of the model to `<model file>.zodar/image<N>_<usage>.ktx2`
(colour: BC7 sRGB; data and normal maps: BC7 linear with the box / normal-vector mip filter)
and prints size and PSNR per texture. `loadGltf` (ModelViewer, ZodarBench, games) picks the
compiled files up automatically while the source images are unchanged; edit a source image and
the stale file is ignored until the model is recompiled. Output is identical for any thread
count. Independent validation: `ktx validate <file.ktx2>` from KTX-Software, if installed.

Use `--no-validation` for timing runs (validation layers add CPU cost) and a separate run with
validation for correctness. Reports from a software rasterizer set `"softwareRasterizer": true`.

## ZodarUISandbox (temporary)

Review tool for the UI module, drawn with SDL3's 2D renderer until the Vulkan UI pass exists.

```
ZodarUISandbox [--scale F] [--user-scale F] [--size WxH] [--frames N] [--screenshot F.png]
               [--check] [--software] [--resources DIR]
```

Loading window (Stage UI-2), driven by **mock** startup data (scripted example values, not engine
progress; the window title and every log line say so):

```
ZodarUISandbox --screen loading [--scenario normal|slow-shaders|failure|indeterminate]
               [--at SECONDS] [--speed F] [--monitor WxH] [--open-log] [--exit-after-launcher]
```

`--at` freezes mock time (screenshots), `--monitor` simulates a monitor size, `--speed` speeds up
a live run. A live run shows the loading window, then resizes and re-centres the same window for
the launcher placeholder. `ctest` runs one `ui.loading_*` test per scenario and `ui.loading_flow`.

`--scale` overrides the OS display scale (test profiles 1.0 / 1.33 / 2.0; any value works),
`--check` verifies token colours at known pixels (exit code 4 on mismatch), `--software` forces
SDL's software renderer. `ctest` runs it offscreen as `ui.sandbox_smoke`.

## Windows build from Linux (MinGW-w64)

For cloud-built Windows test binaries (the primary Windows toolchain stays MSVC):

```bash
sudo apt install g++-mingw-w64-x86-64-posix
```

```bash
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake -DZODAR_GPU_TESTS=ON
```

```bash
cmake --build build-windows
```

Executables in `build-windows/bin` are statically linked (only Windows system DLLs). Copy them
with the `shaders/` folder; `ZodarGpuTests` finds shaders next to the executable or via
`ZODAR_SHADER_DIR`.
Khronos sample models (e.g. `DamagedHelmet.glb`) are good test inputs; they are not part of
the repository.

## Headless / cloud testing

Without a GPU, Mesa **lavapipe** (`mesa-vulkan-drivers`) provides a conformant software
Vulkan 1.3+ device. Two ways to get a presentable surface:

```bash
SDL_VIDEO_DRIVER=offscreen ctest --test-dir build --output-on-failure
```

```bash
xvfb-run -a ctest --test-dir build --output-on-failure
```

This verifies correctness (validation-clean API usage, pixels in the readback). It says
**nothing about GPU performance**; FPS numbers from lavapipe are meaningless. The log warns
when a software rasterizer is selected.
