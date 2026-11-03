# Action input and deterministic replay (D3–D4)

`Zodar::Input` owns main-thread control state, action snapshots, and context selection.
`Input/SDLInput.cpp` translates SDL events; game simulation reads only `input().action(name)`.
`ApplicationDesc::inputMap` opts an app in. `--input-map FILE` overrides game defaults.
The runtime filters overlay-consumed presses/motion (but always delivers releases), feeds input
before `onEvent`, samples before each `onFixedUpdate`,
and applies requested relative mouse mode to its window. Playback ignores live gameplay events;
Escape/window close can still abort a run. Gamepad devices are opened/closed by the runtime.

## Maps

`game/assets/input.json` contains the actual game bindings. Minimal map:

```json
{"schema":"zodar.input_map","version":1,"contexts":[
  {"name":"game","priority":0,"actions":{
    "jump":{"type":"button","bindings":[{"source":"key","code":"Space"}]},
    "move":{"type":"axis2","bindings":[
      {"source":"key","code":"W","scale":[0,1]},
      {"source":"key","code":"S","scale":[0,-1]}]}
  }}
]}
```

Actions are `button`, `axis1`, or `axis2`. Bindings use `key` (SDL scancode names),
`mouseButton` (`left|right|middle`), `mouseDelta` and `wheel` (`x|y`), `gamepadButton`
or `gamepadAxis` (SDL gamepad names, e.g. `south`, `leftx`). Optional `scale:[x,y]`,
`invert`, and scalar `deadZone` in [0,1) apply per binding. Axes sum; consumers normalize
movement as appropriate. Gamepads share a logical input channel; there is no player/device routing.
All contexts start enabled. `setContext(name,enabled)` selects modes. Higher priority contexts
consume their bound controls and override same-named actions; ties use registration order.
Bindings within one context may intentionally share a control (e.g. Space for fly-up and jump).
Unknown fields, action types, binding names and invalid numbers raise `DataError`.
Serialization is canonical; map identity is a stable 64-bit FNV-1a content hash, not a security signature.

Snapshots expose `x`, `y`, `held`, `pressed`, `released`. Edges accumulate between ticks,
including a complete press/release tap. Repeated keydown events do not retrigger. Held buttons
persist; mouse/wheel deltas accumulate, are consumed by the next tick, and then reset.
No tick means no input is consumed. Focus loss/device removal clears held physical controls.
`feed(source,code,value)` is the platform-independent control feed.
`inject(action,ActionState)` overrides an action for the next tick (button edges are derived;
explicit edges preserve short taps). Replays inject a complete snapshot every tick.

The game retains fly-camera controls, Highlands left-click dig/F place, and bunker
`--explore` walking, sprint, jump, look and Tab capture. Arrow-key movement is exploration-only.
Movement/look/edit simulation now runs in fixed updates. This adds up to one tick of input
latency; it removes render-frame dependence. No scheduler, doors or power logic was added.

## Record and replay

```bash
./build/gpu-tests/bin/LastlightExpanse --level bunker --explore --record-input walk.inputrec.jsonl
./build/gpu-tests/bin/LastlightExpanse --level bunker --explore --headless --replay-input walk.inputrec.jsonl
python3 tools/zodar.py replay walk.inputrec.jsonl -- --level bunker --explore
```

For repeatable pixel comparisons also record with `--deterministic --frames N`.
Recordings made with variable render frame rate still record every fixed simulation tick.
Bunker recording waits for its assets before simulation to give playback the same initial state.
Use identical level/mode, seed (game default 1337), fixed-dt (default 1/60), and initial camera
options. A nondefault seed/fixed-dt must also be passed to playback. Replay forces deterministic
stepping and ends at the recording tick count. If the final render is skipped, simulation and
visual time freeze while the final frame/capture is retried. `--frames N` permits a shorter prefix; exceeding the
recording length is an error. Headless replay does not need an explicit frame count.

`zodar.input_record` v1 JSONL has a header (app identifier, canonical map hash, level hash,
seed, fixed-dt), contiguous 1-based ticks with delta-encoded action snapshots and post-tick
state fingerprints, and a mandatory footer with tick count and checksum. A tick action is
`[x,y,held,pressed,released]`. The first tick declares the complete action set. Duplicate keys,
unknown/new actions, wrong types, discontinuous ticks, corruption, missing footer/newline and
truncation fail before playback. Files are atomically replaced only after successful recording.
Shutdown, capture or report failures preserve the previous recording; long captures currently retain snapshots in RAM.

Map/level mismatch fails unless `--replay-lenient`; app/seed/fixed-dt and action-set checks
remain strict. Lenient mode does not disable state verification. Run reports include
`input.mode`, `mapHash`, `ticks` and final `stateHash`.

Applications override `inputLevelHash()` and `inputState()` for content identity and simulation
verification. The runtime combines game state with named RNG states and checks after every tick.
Bunker identity hashes runtime asset bytes (excluding source-project/cache directories); state
covers walker position/velocity, orientation, grounding, mouse capture and waypoint progress.
Highlands identity includes generator version, seed and radius; state covers camera and an edit
command digest. These are targeted fingerprints, not a complete world/save snapshot. The edit digest includes target coordinates, change results
and resulting voxel values. Hand-authored
ticks may use `state:""` to omit checkpoints. Same-build/platform determinism is tested;
floating-point equality across architectures is not guaranteed. Render timing/statistics and
asynchronous visibility are not simulation state. Recorded/deterministic Highlands runs advance streaming and wait for jobs at each fixed tick,
before querying/editing terrain; normal interactive streaming remains asynchronous.

## Randomness

`Core/Random.h` provides per-instance `Random(seed)`, `next(name)` (SplitMix64) and `unit(name)`
([0,1), explicit conversion, no implementation-defined distribution). Names get stable independent
streams, so calls to another stream cannot perturb a system. `Application::random()` is seeded
from `--seed` (0 for apps without a seed). `states()/seed()/restore()` support future save
continuation. Use from fixed-step simulation on one thread; no global RNG or wall-clock seed.
Existing deterministic terrain/noise algorithms remain unchanged.

## Validation

Focused doctest cases are `input:*`, `replay:*`, and
`region: partial worker failure drains before propagation`. `gpu.lastlight_input_replay` checks
both levels: record/replay pixel equality, two moving hand-authored replays, state divergence
rejection, strict/lenient compatibility, and variable-frame recording. Existing game, asset,
UI and Vulkan checks remain enabled. D5 scheduling/events/timers and D6 save/load are separate tasks.

Review regressions: `gpu.input_runtime_regression` injects a minimized final frame, shutdown
failure, unwritable screenshot and UI capture. It verifies exact replay tick counts/time,
previous-recording preservation and release delivery without UI click-through.
