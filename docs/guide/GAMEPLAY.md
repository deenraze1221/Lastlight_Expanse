# Deterministic gameplay execution

`Zodar::Gameplay` is an engine-only, CPU-only module depending on Core. Include
`Gameplay/Gameplay.h`. `Application::gameplay()` owns one scheduler, event bus and timer
clock per app. They run sequentially on the application thread; they are not thread-safe.
There is no global registration/state and no player or Lastlight concept in the engine API.

## Systems

Register once during `onStart()`:

```cpp
gameplay().systems().add({
    "simulation.move", zodar::GameplayPhase::FixedSimulation,
    [this](const zodar::SystemContext& c) { move(c.dt); }
});
gameplay().systems().add({
    "simulation.interact", zodar::GameplayPhase::FixedSimulation,
    [this](const zodar::SystemContext&) { interact(); },
    {}, {"simulation.move"} // before, after
});
```

`SystemDesc` contains `name`, `phase`, `run`, `before`, `after`. Names are unique across the
scheduler. Forward references are allowed. Validation occurs after `onStart`, before the first
tick, and freezes registration. Adding systems afterward, recursive phase execution, duplicate
or missing names, empty callbacks, cycles and dependencies reversing phase order throw `Error`.
Same-phase ordering is a stable topological sort: the earliest registered ready system wins
at every choice. Duplicate edges count once. A forward cross-phase dependency is already
satisfied by phase order; it does not force a phase to execute at another phase's frequency.

`SystemContext` provides `dt`, `tick` and `alpha`. Fixed context uses the fixed dt, the 1-based
executing tick and alpha 0. Presentation context uses frame dt, the last completed tick and
interpolation alpha. `LateUpdate` and `RenderSync` must not advance simulation, RNG, timers or
emit simulation events: their call count depends on rendering. Fixed systems must not read
wall time or SDL directly; use sampled/injected input. Callbacks own their game state normally.

| Order | Frequency |
| --- | --- |
| Replay injection / input sampling | Each executed fixed tick |
| Input → FixedSimulation → Animation systems | Each executed fixed tick |
| Legacy `onFixedUpdate(dt)` | After those systems, once per tick |
| Due timer callbacks → event-buffer flip | End of the same tick |
| Completed-tick boundary → `onCheckpoint(N)` → replay fingerprint/record | Once after successful tick processing |
| LateUpdate systems → `onUpdate` → RenderSync systems | Every loop iteration, including skipped render attempts |
| Render/overlay/present → `onFrameEnd` | Only when rendering succeeds |

The existing hooks remain compatible with an empty scheduler. Do not call migrated logic from
both a system and a hook. Callback failures propagate and abort the application; there is no
tick rollback or retry contract. A failure during fixed processing does not establish a completed checkpoint; an observer
exception aborts the run without rolling back the already-completed tick.

## Typed events

```cpp
struct Used { unsigned object; };
gameplay().events().emit(Used{17});
for (const auto& e : gameplay().events().read<Used>()) { /* read-only current batch */ }
```

`emit(T)` appends a value to that type's pending batch. Systems, fixed hooks and timer callbacks
in tick N cannot read it until tick N+1. Order within a type is emission order. There are no
subscriptions, callbacks or cross-type iteration/delivery ordering. Reading an unknown type
returns an empty span without creating a channel. Spans remain valid until the next buffer
flip; emitting never invalidates the readable batch. A batch expires at the next flip even
if nobody read it. Payloads should be plain simulation values, not thread-shared mutable state.

The runtime flips once per successful tick after timer dispatch. Outside fixed processing,
`emit` only queues for the next end-of-tick flip; seed initial state directly and do not drive
simulation by emitting from presentation hooks. `EventBus::advance()` exists for standalone
CPU use; the runtime owns it inside Application. Persistent channels use the explicit D6 event codecs described in SAVES.md.

## Fixed-tick timers

```cpp
auto id = gameplay().timers().after(60, [this] { pulse(); }, 60); // delay, callback, repeat
bool cancelled = gameplay().timers().cancel(id);
```

`after(delay,callback,repeat=0)` requires delay >= 1; repeat 0 means one-shot. Deadlines are
relative to `timers().tick()`: 0 during startup, N while executing tick N and at checkpoint N.
A delay-1 timer created during tick N first fires at the end of N+1. A startup delay-1 timer
fires at the end of tick 1. Callbacks run after the fixed hook, ordered by deadline then
monotonic timer ID. Repeating deadlines advance by their interval, without render-time drift.
IDs are not reused; zero/overflow deadlines and recursive clock advancement are errors.

Callbacks may cancel themselves/others or schedule future timers; zero-delay recursion is
forbidden. A repeating timer is rescheduled before its callback; a one-shot is removed first. Mutable
capture state persists across repeat firings, and dispatch retains callable ownership through
self-cancellation.
`cancel` returns false for an unknown/already-fired ID. New timers cannot fire in the same
boundary. Wall time discarded by FixedTimestep does not advance timers; every executed tick
advances exactly once. `Timers::advance()` is for standalone CPU tools/tests, not app callbacks.

## Profiling

Each callback gets a `System/<name>` CPU scope. Multiple calls in a multi-tick frame produce
multiple samples, and skipped render attempts retain CPU scopes in run reports. GPU/frame
statistics retain their existing rendered-frame meaning. Scope lifetimes close on exceptions.

## Checkpoint boundary

After systems and the legacy fixed hook, due timers run, events flip, and the completed-tick
counter advances. `onCheckpoint(uint64_t N)` is called once there, before the replay state
fingerprint is verified/recorded. It is an observation boundary: do not mutate simulation or
emit more events from it. The next zero-based input/replay cursor is N (next recording row N+1).
Use `gameplay().completedTicks()`, not `timestep().totalSteps()`: the latter may already count
all steps in a frame while an earlier one is executing.

At that boundary pending timer deadlines and readable next-tick events are part of simulation
state. D6 serializes/reconstructs them together with registered component/game state, RNG,
input edges and replay position through staged commits (SAVES.md). Replay fingerprints still
use app-selected state plus named RNG; apps must expose any new meaningful system state in `inputState()`.

## Game integration and validation

Highlands runs Camera → Terrain → Interaction as ordered fixed systems; bunker runs Controls
→ Interaction → Doors → Power in FixedSimulation and derived CameraSync in RenderSync.
Existing action bindings, movement math, streaming rules and edit behavior remain. Animation is available but unused by games.

Focused checks: `ZodarTests --test-case='gameplay:*'` covers graph validation/ties, event batches,
timer ordering/repeat/cancel, checkpoint semantics, multi-step frames and exception profiling.
`gpu.input_runtime_regression` checks all runtime phases, input-before-systems, timer-generated
N+1 events, one checkpoint per tick, catch-up frames, frozen final replay frames and profiler
counts. Existing empty-scheduler runtime tests and both-level record/replay pixel/state tests
remain enabled. Before pushing, run `tools/gate.sh` (Debug/Release, warnings as errors, all CTest
and Vulkan checks) with the established Highlands hash. Bunker system contracts and controls
are in BUNKER_GAMEPLAY.md.

## Checkpoints and persistence

See [SAVES.md](SAVES.md) for the completed-tick checkpoint contract, named persistent timers,
event codecs, input/RNG restoration, and nonthrowing staged commits. Anonymous pending closures
and nonempty unregistered event channels cannot be saved. Game-only components and stable-ID
interactions are documented in BUNKER_GAMEPLAY.md.

The [CPU input replay example](../../examples/input_replay/README.md) shows one fixed system
using action snapshots and named RNG. Application owns the loop in real games; do not call
`beginStep()` or `Gameplay::step()` again from registered callbacks.
