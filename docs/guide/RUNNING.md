# Running ZODAR apps (build, test, run, capture, report)

How to build, test, run and capture every ZODAR application from the command line. Written
for agents and humans; every command prints a short summary and writes full logs to files.

## 1. Build and test

CMake presets (`CMakePresets.json`, build tree `build/<preset>`, CMake >= 3.25, Ninja):

| Preset | Build type | Tests registered | Use |
| --- | --- | --- | --- |
| `debug` | Debug | unit tests | quick edit/test loop without a GPU |
| `release` | Release | unit tests | benchmarks, FPS numbers |
| `gpu-tests` | Debug | unit + GPU tests | default for `tools/zodar.py`; needs a Vulkan device (lavapipe works) |
| `ci` | Debug | unit + GPU tests, warnings as errors | what must pass before a commit |

```bash
cmake --preset gpu-tests
cmake --build --preset gpu-tests
ctest --preset gpu-tests
```

```bash
cmake --workflow --preset ci
```

Test presets set `SDL_VIDEO_DRIVER=offscreen`, so GPU tests run without a display.

## 2. `tools/zodar.py` (short output)

Python 3.8+, standard library only. Global options come before the command:
`--preset P` (default `$ZODAR_PRESET` or `gpu-tests`) or `--build-dir DIR` (any configured tree).
Arguments after `--` go to the app unchanged. The exit code is the build's, test run's or app's.

| Command | Does | Prints |
| --- | --- | --- |
| `zodar.py build [TARGET...]` | configures on first use, builds | compiler errors/warnings only (max 30), status, log path |
| `zodar.py test [-R REGEX] [--tail N]` | ctest (JUnit parsed) | each failing test with its last N output lines, pass/fail counts |
| `zodar.py run APP [--tail N] -- ARGS` | runs the app | last N log lines, exit code |
| `zodar.py shot APP [--frames 120] [--out zodar-shots] -- ARGS` | headless deterministic run with `--screenshot` + `--report` | run report summary, PNG and report paths (exit 3 if either is missing) |
| `zodar.py bench -- ARGS` | ZodarBench | benchmark summary (CPU/GPU frame, top passes, draws) |
| `zodar.py report FILE.json` | — | summary of a run report or ZodarBench report |
| `zodar.py import FILE... [--id ID] [--compile] ...` | ZodarAssetTool import | one line per imported asset (id, type, hash, dependencies) |
| `zodar.py assets list\|check [--report F] [--strict] [--palette P]` | ZodarAssetTool | registry listing; check: one line per problem + summary, JSON report (`docs/guide/ASSETS.md`) |

`APP`: `game` (LastlightExpanse), `viewer` (ModelViewer), `bench` (ZodarBench), `runtime`
(ZodarRuntimeSmoke) or any executable name in `<build>/bin`. Logs: `<build>/zodar-logs/`
(build, test, run, bench) and `<out>/<App>.log` (shot).

```bash
python3 tools/zodar.py build
python3 tools/zodar.py test -R runtime
python3 tools/zodar.py shot game --frames 120                       # bunker (default level)
python3 tools/zodar.py shot game --frames 120 -- --level highlands  # regression scene
python3 tools/zodar.py shot viewer --frames 30 -- --width 640 --height 360 --debug-view normals
python3 tools/zodar.py --preset release bench -- --scene city --blocks 6 --no-validation
```

## 3. Standard options (every `zodar::Application` app)

`LastlightExpanse`, `ModelViewer` and `ZodarBench` accept the same options
(`Runtime/CommandLine.h`; `--help` prints them with the app's own options):

| Option | Meaning |
| --- | --- |
| `--frames N` | render N frames, then exit (ZodarBench: N measured frames) |
| `--headless` | no window (SDL offscreen driver); needs `--frames` |
| `--deterministic`, `--fixed-dt S` | one fixed step of S seconds (default 1/60) per frame: same input, same frames |
| `--width W --height H` | render size |
| `--screenshot F.png` | save the last frame (ZodarBench: the `--screenshot-frame` measured frame) |
| `--capture-frames 10,60,120 --capture-dir DIR` | also save these 1-based frames as `DIR/frame_NNNNNN.png` |
| `--report F.json` | run report at exit (schema `zodar.run_report` v1) |
| `--log-file F` | human-readable log copy |
| `--log-json F.jsonl` | structured log, one JSON object per line |
| `--seed N`, `--camera X,Y,Z,YAW,PITCH` | app-defined seed; start camera (game, ModelViewer) |
| `--threads N` | worker threads (0 = inline jobs) |
| `--no-validation`, `--sync-validation` | Vulkan validation off / plus synchronization validation |
| `--overlay`, `--no-overlay` | profiler overlay at start (F1 toggles) |

Exit codes: `0` ok, `1` fatal error or bad arguments (or unwritable report), `2` Vulkan
validation errors, `3` a requested screenshot or capture was not written.

## 4. Outputs an agent reads

- **PNG screenshots / captures**: RGBA8 of the presented image, overlay included when visible.
- **Run report** (`--report`): `run` (frames, fixed steps, wall/simulated seconds, size, seed),
  `device`, `validation`, `log` (warning/error counts), `fps` p50/p95/p99, `frameMs`,
  `cpuFrameMs`, `gpuFrameMs`, `cpuScopes`, `gpuPasses`, `render` (last frame stats),
  `memory` (last + peak), `screenshots` (path, frame, ok). Standard CPU scopes:
  `Fixed update`, `Update`, `Wait + acquire`, `Build graph` (onRender + overlay), `Record`,
  `Submit + present`; apps add their own inside these.
- **JSON-lines log** (`--log-json`): `{"time":"2026-01-02T03:04:05.678Z","level":"info","thread":1,"msg":"..."}`;
  levels `trace|debug|info|warning|error|critical`. Filter with `grep '"level":"error"'`.
- **ZodarBench report** (`--output`, default `zodarbench.json`): benchmark statistics over the
  measured frames (see the header of `engine/tools/ZodarBench/main.cpp`).

On a software rasterizer (lavapipe/llvmpipe) GPU times are not GPU performance; compare runs
interleaved and treat ±5% as noise.

## 5. Writing a new app

Derive from `zodar::Application` (`engine/runtime/Runtime/Application.h`) and use `runMain`;
the standard options, captures, report and exit codes come for free:

```cpp
class MyApp final : public zodar::Application {
public:
    MyApp(const zodar::ApplicationDesc& desc) : Application(desc) {}
protected:
    void onStart() override { /* load scene, create GPU resources */ }
    void onUpdate(const zodar::FrameTime& t) override { /* camera, animation (use t.dt / t.time) */ }
    void onRender(zodar::RenderContext& rc) override {
        scene().addToGraph(rc.graph, rc.info, rc.extent, m_view, rc.output, rc.format, &rc.stats);
    }
    void onFrameEnd(const zodar::FrameTime&) override { /* per-frame statistics, pacing */ }
private:
    zodar::gfx::SceneView m_view;
};

int main(int argc, char** argv) {
    zodar::ApplicationDesc defaults;
    defaults.name = "MyApp";
    return zodar::runMain(argc, argv, defaults, "MyApp [standard options]",
                          [](const auto& desc, auto args) { return std::make_unique<MyApp>(desc); });
}
```

Reference examples: `engine/samples/ModelViewer` (orbit viewer, A/B reference path),
`engine/tools/ZodarBench` (fixed camera path, frame pacing, custom report),
`game/src/main.cpp` (voxel world, streaming). Use `requestCapture(path)` for screenshots at
app-chosen frames (verified at exit like `--capture-frames`).
