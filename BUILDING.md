# Building RAVEN Engine

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

## Pinned dependencies (fetched by `cmake/RavenDependencies.cmake`)

| Library | Tag | Commit |
| --- | --- | --- |
| SDL3 | release-3.2.24 | a8589a84226a |
| Vulkan-Headers | v1.4.304 | d4a196d8c84e |
| volk | 1.4.304 | 0b17a763ba56 |
| VulkanMemoryAllocator | v3.3.0 | 1d8f600fd424 |
| spdlog | v1.15.3 | 6fa36017cfd5 |
| GLM | 1.0.1 | 0af55ccecd98 |
| doctest (tests only) | v2.4.12 | 1da23a3e8119 |

Headers are 1.4.304 because volk's tags follow header versions; the engine requests and
requires **API 1.3**. Later milestones add Dear ImGui, Jolt, miniaudio, ufbx, cgltf and stb.

## Windows 10/11

1. Install Visual Studio 2022 (Desktop C++), CMake, Ninja, Git and the
   [LunarG Vulkan SDK](https://vulkan.lunarg.com/) (sets `VULKAN_SDK`).
2. Open **x64 Native Tools Command Prompt for VS 2022**:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DRAVEN_GPU_TESTS=ON
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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DRAVEN_GPU_TESTS=ON
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
| `RAVEN_BUILD_SAMPLES` | ON | Build `Samples/` |
| `RAVEN_BUILD_TESTS` | ON | Build `RavenTests` (doctest) |
| `RAVEN_GPU_TESTS` | OFF | Register `gpu.hello_triangle` (needs a Vulkan device + video driver) |
| `RAVEN_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` for engine code |

## HelloTriangle arguments

```
--frames N            Render N frames then exit (0 = until closed)
--screenshot F.ppm    Save the last frame (requires --frames)
--no-validation       Do not enable VK_LAYER_KHRONOS_validation
--present MODE        vsync (default) | low-latency | immediate
--width W --height H  Initial window size
```

Exit codes: 0 ok, 1 fatal error, 2 validation errors occurred, 3 screenshot failed.

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
