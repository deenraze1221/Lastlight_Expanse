# RAVEN Engine — Architecture

## Principles

1. **Universal engine, no enforced style.** The engine offers rendering, material, lighting
   and post-processing capabilities; art direction belongs to each game.
2. **Engine and games are separate.** Engine modules never contain game rules. A game is a
   separate CMake target under `Games/<Name>/` that links engine modules through their public
   headers.
3. **One module = one static library** (`Raven::Core`, `Raven::Platform`, `Raven::Renderer`, ...).
   Dependencies point downwards only and are enforced by CMake target links.
4. **Explicit ownership (RAII).** Every Vulkan object has exactly one owner whose destructor
   releases it. Owners are non-copyable. No global Vulkan state besides volk's function table.
5. **Fail loudly at init, recover at runtime.** Initialization failures throw `raven::Error`
   with file/line. Expected runtime conditions (swapchain out of date, minimized window) are
   return values, not exceptions.
6. **Build only what a milestone needs.** Libraries and abstractions are added when a real
   feature requires them.

## Module graph

```
                 Games/<Game>      Samples/*      Tests
                       \              |            /
                        +-------- Renderer -------+
                                     |
                                  Platform
                                     |
                                    Core
```

Planned modules (by milestone) slot in as follows: `Scene`, `Assets`, `Physics`, `Audio`,
`Input`, `UI` depend on `Core` (and `Platform` where they touch the OS); `Renderer` consumes
`Scene`/`Assets` data through plain structs; `Networking` depends only on `Core`. `Debug`
(ImGui) depends on `Renderer` + `Platform`. Nothing depends on a game.

## Current modules

| Module | Files | Responsibility |
| --- | --- | --- |
| Core | `Log`, `Error`, `ImageIO` | spdlog-based engine logger, `raven::Error` + `fail()` + `RAVEN_ENSURE`, PPM writer for screenshots |
| Platform | `Platform`, `Window` | SDL3 lifetime, window + event pump, Vulkan WSI glue (loader, instance extensions, surface) |
| Renderer | `Renderer` | Frames in flight, acquire/submit/present, swapchain rebuild, screenshot readback |
| Renderer/Vulkan | `VulkanContext` | Instance, debug messenger, surface, GPU selection, device, queues, VMA allocator |
| | `VulkanSwapchain` | Swapchain, image views, per-image present semaphores, recreation |
| | `VulkanPipeline` | SPIR-V loading, `ShaderModule`, minimal `GraphicsPipeline` |
| | `DeviceSelection`, `SwapchainSupport` | Pure, unit-tested selection policies |
| | `VulkanCheck` | `RAVEN_VK_CHECK`, `VkResult` names |

## Vulkan decisions

| Decision | Reason |
| --- | --- |
| Vulkan 1.3 core, **dynamic rendering** + **synchronization2** required | No `VkRenderPass`/`VkFramebuffer` objects; simpler render graph later. Supported by every current desktop driver and Steam Deck (RADV). |
| **volk** meta-loader, loader obtained from SDL | No link-time dependency on `vulkan-1`; SDL and engine share one `vkGetInstanceProcAddr`. Device-level function pointers skip the loader trampoline. |
| **VMA** for all memory | Standard, correct sub-allocation; first used by screenshot readback. |
| 2 frames in flight, per-frame command pool reset | Bounded CPU/GPU overlap, cheap command recording. |
| Present semaphores **per swapchain image** | A present may still hold a semaphore when its frame slot is reused; per-image avoids the reuse hazard flagged by current validation layers. |
| Fence reset **after** successful acquire | An out-of-date early return never leaves a fence unsignalled (deadlock). |
| sRGB swapchain (`B8G8R8A8_SRGB` preferred) | Shaders write linear colour. Scenes will render to an HDR target and resolve into this through the post-process stack. |
| Validation on by default, errors counted | `HelloTriangle` exits with code 2 if any validation error occurred; the GPU test relies on it. |

## Frame flow (Milestone 1)

```
beginFrame()
  resize pending? -> waitIdle, recreate swapchain
  wait fence[slot] -> acquire(imageAvailable[slot]) -> reset fence, reset pool
  begin cmd, barrier UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
caller records: vkCmdBeginRendering ... draw ... vkCmdEndRendering
endFrame()
  [screenshot? copy image -> host buffer]
  barrier -> PRESENT_SRC, end cmd
  submit2(wait imageAvailable[slot] @ colour output, signal renderFinished[image], fence[slot])
  present(wait renderFinished[image]); OUT_OF_DATE/SUBOPTIMAL -> rebuild next frame
```

In Milestone 1 the sample records raw Vulkan commands between `beginFrame` and `endFrame`.
Milestone 2 introduces render passes/graph, meshes and materials so games no longer touch
Vulkan directly.

## Rendering: capabilities, not styles

RAVEN does not ship visual styles or "profiles". The engine provides general-purpose
rendering capabilities; each game composes them into its own look.

| Engine provides (style-neutral) | Game decides |
| --- | --- |
| Materials as data: shader + parameters + textures, user-authored shaders | Which shaders and materials exist, their look |
| Built-in PBR metallic-roughness shading model *as one option*, plus custom/unlit models | Whether to use PBR at all |
| Lights (directional, point, spot), shadow maps with configurable resolution/filtering | Light setup, shadow quality per scene |
| Render resolution scale or fixed internal resolution + selectable upscale filter (linear/nearest) | e.g. 640×360 nearest for a pixel look |
| Sampler control per texture/material (filtering, anisotropy, mip bias) | Point-sampled vs filtered textures |
| Vertex-stage material hooks (custom vertex functions) | Vertex snapping, wobble, deformation |
| Post-process stack of ordered, data-driven passes (built-ins: tone mapping, bloom, SSAO, fog, colour LUT) + custom passes | Which passes, their order and parameters (dither, quantize, CRT, ...) |
| `GraphicsSettings` (resolution, scale, shadow/AO/bloom quality, vsync, anisotropy) | Defaults and user-facing presets ("Low/High", "Authentic PSX") |

Rule: if a feature only makes sense for one art direction, it lives in the game. If several
very different games could use it, it is an engine capability with neutral defaults.

DEAD SECTOR's PSX-inspired look is therefore a **game-side style preset**
(`Games/DeadSector/Rendering/`): its own shaders (vertex snapping, affine UV option,
dither/quantize post pass), material conventions and settings presets, built only from
public engine capabilities.

## Conventions

- Namespaces: `raven` (core/platform), `raven::gfx` (renderer). Types `PascalCase`, functions
  `camelCase`, members `m_name`, constants `kName`, macros `RAVEN_`.
- Headers are included as `"Module/File.h"` relative to `Engine/`.
- Third-party code is fetched and compiled as SYSTEM; engine code is built with
  `-Wall -Wextra -Wpedantic -Wshadow` (or `/W4`), optionally `-Werror`.
- Shaders: GLSL → SPIR-V at build time (`raven_compile_shaders`), output to `bin/shaders/`.
  Engine-wide shaders will live in top-level `Shaders/`; sample-only shaders stay with the sample.

## Known architectural debts (tracked)

- A pipeline is created for a fixed swapchain format; if a recreated swapchain changes format,
  pipelines must be rebuilt (not handled yet; formats do not change in practice).
- Swapchain recreation uses `vkDeviceWaitIdle`; `VK_EXT_swapchain_maintenance1` present fences
  would remove the stall and the theoretical semaphore-reuse gap on recreate.
- One window per process (`Platform` owns SDL init).
