# CLAUDE.md

Instructions for Claude Code sessions in this repository.

## Context Efficiency Rules

- Read only files relevant to the current task.
- Never scan the entire repository unless necessary.
- Avoid printing full source files and build logs.
- Use targeted searches and summaries.
- Do not repeatedly inspect unchanged files.
- Run focused tests instead of full test suites when possible.
- Keep responses concise.
- Do not repeat previous explanations.
- Maintain PROGRESS.md with completed work, current task and next steps.
- Maintain ARCHITECTURE.md with important technical decisions.
- Before ending a session, update project documentation.
- Preserve all existing functionality and architecture.

## Project Quick Reference

- Repo: Lastlight Expanse game (`game/`) on the reusable ZODAR engine (`engine/`). Engine code
  never depends on `game/`. Layout and module rules: `docs/ARCHITECTURE.md`.
- Status and next task: `PROGRESS.md`; plan: `docs/AI_FIRST_ROADMAP.md` (Phase A done, B1 done, B2 done, C1+C2 done: `Zodar::AssetManager` incl. async loads, guide `docs/guide/ASSETS.md`; next per `PROGRESS.md`).
- Token-efficient session workflow: `CLAUDE_CODE_WEB_EFFICIENT_WORKFLOW.md`.
- Art direction (voxel/pixel art, texture sizes, Blockbench → glTF workflow): `docs/ART_DIRECTION.md`.
- Language **C++23** (GCC 13, Clang 18, MinGW-w64 verified; MSVC unverified). Use only features
  listed as usable in `BUILDING.md` ("C++23 policy").
- Vulkan **1.3 minimum**, 1.4 requested when available (`engine/graphics/Renderer/Vulkan/ApiVersion.h`).
  No 1.4 functionality may be used without a capability entry and a 1.3 fallback.
- Headers are included as `"Module/File.h"` (each module adds its own include root).

## Build and Test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DZODAR_GPU_TESTS=ON
cmake --build build
SDL_VIDEO_DRIVER=offscreen ctest --test-dir build --output-on-failure
```

- Short output: `python3 tools/zodar.py [--preset P | --build-dir DIR] build|test|run|shot|bench|report`
  (presets `debug|release|gpu-tests|ci` in `CMakePresets.json`; guide: `docs/guide/RUNNING.md`).
- Focused: `./build/bin/ZodarTests -tc='name*'`, `./build/bin/ZodarGpuTests -tc='name*'`.
- Game smoke: `./build/bin/LastlightExpanse --headless --deterministic --frames 60 --screenshot out.png --report run.json`
  (standard options of every `Application` app: `--help`).
- New apps derive from `zodar::Application` (`engine/runtime/Runtime/Application.h`); game,
  ModelViewer and ZodarBench do (ZodarBench: `--frames` = measured frames).
- Benchmarks: `ZodarBench ... --no-validation --output report.json`; on lavapipe (software)
  compare runs interleaved and treat ±5% as noise.
- Cloud containers: needs `glslang-tools`, `mesa-vulkan-drivers`, `vulkan-validationlayers`
  (Ubuntu's layer is 1.3.275, so validated runs use Vulkan 1.3). Keep build dirs outside the
  repo; give each build tree its own `FETCHCONTENT_BASE_DIR` copy (shared `*-build` dirs collide).

## Rules

- Commit and push only when builds, tests and GPU validation pass; report results honestly.
- Preserve working systems; extend through new layers instead of rewrites.
- Do not start roadmap phases or modify unrelated systems unless asked.
