# World Production Roadmap — Lastlight Expanse

Status: **approved in principle (2026-11-01); planning only, nothing implemented from it.**
Every area of the game is developed into its intended final-game state, one area at a time.
The bunker is the first production-complete area; its workflow, quality bar and checklist are
the template for every later area.

Labels used below: **[GDD]** requirement from `docs/GDD/Lastlight_Expanse_GDD_v1.1.md`;
**[Done]** existing implementation; **[Approved]** project-owner decision; **[Proposed]**
suggestion awaiting approval (not a requirement); **[Decision]** open question for the
project owner. Proposed areas, layouts, sizes, map positions, story events, NPCs, loot tables
and gameplay are never requirements until approved.

## 1. Completion levels

| Level | Meaning |
| --- | --- |
| **Engine Complete** | Every generic ZODAR system the game needs exists, is documented and tested, and meets performance targets on the reference hardware: runtime, ECS, assets, voxels (Phase V), input/replay/scheduler/save (Phase D), rendering S5–S8 and weather rendering, physics/audio/animation (Phase E), navigation/AI framework, networking, UI, tooling. No Lastlight content. |
| **Area Production Complete** | One area passes the full checklist (§4), **including its required co-op behaviour** [Approved]. An area may first reach a separate **Single-Player Complete** milestone (checklist minus co-op); final sign-off stays pending until networking is done and tested. |
| **Game Complete** | Every area Production Complete, plus cross-area systems (story arc §5, progression and economy balance §9, 48 h rotation §4, world map and Fog of War §2/§12, saves and co-op stability §11) and release checks on the target hardware (§13). |

## 2. Order (approved; unchanged)

```
V1..V5 -> D3..D6 -> D7 -> V6 -> D8            (approved Phase V / D order, docs/PHASE_D_PLAN.md)
After D8: R (S5 shadows, S6 tonemapping/TAA, S7 AO, S8 volumetrics)   [Approved placement]
          before the bunker's final presentation and sign-off
Further tracks after D8 (order [Proposed], see §6): E, G1-G4, N1, W0, B, M, then later areas
```

- Rendering work is **not** moved before D8 [Approved].
- D7 ("playable bunker") is part of the bunker's stage A2, not its completion.
- The working D2 bunker (default level, exploration mode, walkthrough and smoke tests) stays
  intact until each replacement (V5 voxel architecture, D7 controller, final assets) is
  tested [Approved].

## 3. Production stages per area

| Stage | Content | Gate |
| --- | --- | --- |
| A0 Brief | Purpose, layout, gameplay, NPCs, enemies, loot, events and story traced to the GDD; proposals marked | Project owner approves the brief |
| A1 Structure | Voxel architecture at final scale, traversal, collision, navigation data | Walkthrough test covers every space |
| A2 Systems | Gameplay, NPCs, enemies, loot and events wired through input actions, systems, events and saves | Replay test of the area's core loop |
| A3 Content | Final models, textures, audio, storytelling items | **Asset gate** (§5) for every asset |
| A4 Presentation | Lighting, shadows, AO, fog/volumetrics, weather (outdoor), audio mix | Project owner approves screenshots/captures |
| A5 Sign-off | Performance, regression, checklist (§4); co-op tests when networking exists | Single-Player Complete, then Production Complete |

## 4. Area completion checklist

1. Brief approved; all content traceable to the GDD or an approved decision.
2. Architecture entirely voxel geometry at final scale; no greybox modules.
3. Final assets only: models and textures within the texture policy (32 small details, 64
   standard, 128 characters/weapons only, ≥ 256 and 8/16 not allowed), `assets check --strict`
   clean (palette, texel density, budgets, bounds); each asset passed the asset gate.
4. Presentation: shadows, AO, fog/volumetrics; outdoor: day/night and weather (§4); owner-approved
   captures.
5. Gameplay per GDD through production systems; no temporary controllers or debug paths.
6. NPCs, enemies, loot and events where the GDD (or an approved brief) specifies them.
7. Traversal: collision, AI navigation, full player traversal; walkthrough test or replay.
8. Audio: ambience, surface footsteps, reverb, occlusion, interaction sounds.
9. Persistence: area state survives save/load (§11).
10. Co-op behaviour implemented and tested (required for Production Complete).
11. Quality: Debug + Release 0 warnings, 0 validation messages, golden captures, replay,
    performance budget on RTX 4070-class and a weaker reference PC (owner hardware), Highlands
    regression byte-identical.
12. No temporary or placeholder asset or system unless the owner approved it in writing
    (recorded in the area's sign-off).
13. Docs: area guide, handoff, signed checklist.

## 5. Asset gates and sourcing

- **Tools [Approved]:** Blockbench primary (voxel/low-poly models); Blender for complex and
  organic models; unified glTF pipeline (D1).
- **Gate [Approved]:** generated 64×64 voxel textures become final only after the owner's
  visual approval. Placeholder or generated models are never used as final assets.
- **Gate per asset:** source file committed (`.bbmodel`/`.blend`), strict check clean, visual
  approval recorded in the area's asset list.

External asset requirements for the bunker and **proposed** sources:

| Asset | Requirement | Proposed source |
| --- | --- | --- |
| Voxel block textures (concrete, paint, steel, floor, markings) | 64×64, palette | Generated by script, then owner approval; or hand-painted |
| Furniture, doors, frames, lights, signs, vents, trays, lockers, counters, shelves | Final Blockbench models, 64×64 | Owner/artist in Blockbench |
| Generators, machinery, terminals | Final models | Blockbench, or Blender for complex parts |
| Five merchants (+ approved optional NPCs) | Character models, 128×128, rig, animations (idle, talk, gesture, walk) | Owner/artist (Blockbench); animation in Blockbench/Blender |
| Weapons on display (weapon dealer) | 128×128 models | Owner/artist (Blockbench) |
| Audio: ventilation, generator, lights hum, doors, footsteps (concrete, metal), interaction, radio static | Licensed for commercial use | Recorded by owner, or licensed libraries (e.g. CC0 sources); license list per file — **[Decision]** |
| Radio voice messages | Voice lines | Voice actor or approved TTS — **[Decision]** |
| Documents, notes, terminal texts | Text | Drafted here, approved by the owner |
| HUD/UI icons | Pixel art (sizes per policy) | Owner/artist, or generated + approval |

## 6. Tracks and dependencies

| Track | Content | Status | Needed by |
| --- | --- | --- | --- |
| V1–V6 | Voxel foundation, bunker on voxels (V5), performance (V6) | [Approved] | All areas |
| D3–D8 | Input, replay/RNG, scheduler/events/timers, save, playable bunker (D7), audit | [Approved] | All areas |
| R | S5 shadows, S6 tonemapping/TAA, S7 AO, S8 volumetrics | [Approved] after D8, before bunker A4/A5 | All areas |
| E | Audio, animation; Jolt physics (optional for the bunker) | Existing roadmap phase | Bunker A2–A4 |
| G1 | Player survival + HUD: health, stamina, hunger/thirst, medical, death and respawn in the bunker (§8, §12) | [Proposed] | Bunker A2 |
| G2 | Inventory (grid, stacks, weight, slots) + private lockers (§8) | [Proposed] | Bunker A2 |
| G3 | Merchants and economy: inventories, money + barter, availability (§9) | [Proposed] | Bunker A2 |
| G4 | Missions (role pools), XP/levels, 48 h rotation (§4, §9) | [Proposed] | Bunker A2 (selection), outdoor areas (execution) |
| N1 | NPC framework: characters, animation hookup, idle routines, voxel navigation, interaction | [Proposed] | Bunker A2 |
| W0 | Minimal outdoor presentation for the bunker's surface entrance: terrain patch, sky, day/night, weather (§4) | [Proposed] | Bunker A1/A4 (surface part) |
| B | Bunker production A3–A5 | [Proposed] | Single-Player Complete |
| M | Co-op networking (§11): host, LAN/IP, server-authoritative, shared bunker, lockers per player | [Proposed]; required for Production Complete [Approved] | Every area's final sign-off |
| W / C | Full outdoor world (terrain, regions, wildlife) and combat stack (weapons §7, enemies/AI §6, loot §4, map/Fog of War §12) | [Proposed] | Outdoor areas |

Proposed order after D8: R → E → G1 → G2 → N1 → G3 → G4 → W0 → B (Single-Player Complete) →
M → bunker Production Complete → W/C → next area. **[Decision]**

## 7. Bunker (first production area)

### 7.1 Boundary [Proposed]
[Approved] scope: surface entrance, immediate surroundings and the transition into the
underground bunker belong to the bunker area. Proposed exact boundary:
- Underground: the full D2 bunker footprint (≈ 76 × 24 m: exit passage to the generator room)
  plus the stair/ramp transition.
- Surface: one world region (64 × 64 m) centred on the entrance structure (blast door housing,
  stair or ramp head, fence/barrier line, a few static props), ending at a natural edge
  (treeline, fence). Map position: **[Decision]**.
- Consequence: the surface part needs W0 (terrain patch, sky, day/night, weather) before the
  bunker can be signed off.

### 7.2 Audit

| Feature (GDD source) | State |
| --- | --- |
| Rooms: dormitory, locker room, common area, generator room, 5 merchant zones, airlock, exit passage (§10, §9) | [Done] layout (D2 revision) |
| Asset pipeline, registry, strict checks, hot reload, scene loading (C, D1) | [Done] |
| Walkthrough test, collision against level boxes, smoke/validation tests, Highlands regression | [Done] |
| Architecture | Greybox model modules → voxels in V5 |
| 28 models | Greybox (generated "Blockbench-style"), not final |
| Textures | Greybox 32×32 → 64×64 final per policy |
| Exploration walker, `colliders.json`, fly camera | Temporary (D7 controller, V5 voxel collision) |
| Lighting | Partial: point lights, emissive, ambient, fog; no shadows/AO/volumetrics/tonemapping (R) |
| Doors, generator, repairable panels, emergency lights (§4) | Missing (D7) |
| Input actions, replay, scheduler/events, saving (§11) | Missing (D3–D6) |
| Survival stats, medical, death/respawn, HUD (§8, §12) | Missing (G1) |
| Inventory, private lockers (§8) | Missing (G2) |
| Merchants: trading, money/barter, availability (§9) | Missing (G3, N1) |
| Missions, XP/levels, 48 h rotation (§4, §9) | Missing (G4) |
| NPC characters, animation, navigation, interaction (§10) | Missing (E, N1) |
| Audio (§12) | Missing (E) |
| Storytelling: documents, radio, environment (§5) | Missing (proposal §7.4) |
| Surface entrance area | Missing (W0) |
| Co-op shared bunker (§11) | Missing (M) |
| Enemies, combat loot | Not required: the bunker is a safe hub (§10) |
| Real-GPU performance | Not measured |

### 7.3 NPCs
- [Approved/GDD] Required: the five merchants (weapon dealer, medic, resource trader,
  technician, scout), each at their zone with trading and mission selection.
- [Proposed] Optional roles (none approved): radio operator (radio room; delivers broadcast
  story beats), generator mechanic (generator room; ambient), airlock guard (entrance;
  ambient, no combat), cook/quartermaster in the common area (ambient). Proposed population:
  5 merchants + 2–4 optional residents (7–9 NPCs), consistent with "very few living NPCs
  outside the bunker" (§6). **[Decision]**

### 7.4 Story content [Proposed — needs approval before implementation]
Follows §5 (documents, notes, military reports, audio recordings, radio, computers,
environmental clues; minimal dialogue; NIGHTFALL's purpose stays unknown). Proposed items and
placement:

| Item | Placement | Purpose |
| --- | --- | --- |
| Evacuation order (military report) | Airlock bench | Why survivors are here |
| Looping emergency broadcast + intermittent coded transmission naming NIGHTFALL | Radio terminal (scout area or a radio corner) | First hint of NIGHTFALL |
| Survivor notes, missing-person notices | Common area notice terminal/board | Human context |
| Generator maintenance log mentioning power diverted to a restricted facility | Generator room | Environmental clue |
| Map with restricted zones marked | Scout area map table | Points to future areas |
| Corrupted data fragment with a NIGHTFALL header | Technician terminal | Mystery hook |
| Personal diary | Dormitory locker | Emotional note |

### 7.5 Bunker milestones
1. **Single-Player Complete** [Approved concept]: checklist §4 without item 10.
2. **Production Complete**: after M; co-op behaviour (shared bunker, private lockers per
   player, synchronized merchants/missions, host save) implemented and tested.

## 8. Proposed area inventory [Proposed — candidates only, not approved]

Areas derived from GDD §2 region types; names, order, sizes and positions are suggestions.

| # | Candidate area | GDD source | New systems it would introduce |
| --- | --- | --- | --- |
| 1 | Bunker (incl. surface entrance) | §5, §10 | [Approved as first area] |
| 2 | Bunker surroundings / forest edge | §2 forests | Full outdoor world (W), enemies/AI, combat, loot, wildlife |
| 3 | Village | §2 villages | Building interiors, loot refresh, bandit groups |
| 4 | Novograd suburbs | §1, §2 | Dense housing, district electricity (§4) |
| 5 | Forest and wetlands (rivers, lakes, bridges) | §2 | Water, vegetation wind, wildlife habitats |
| 6 | Industrial area | §2 | Machinery, restorable power |
| 7 | Novograd city districts | §1, §2 | Large urban streaming, static vehicles |
| 8 | Military site | §2, §6 | Former military enemies, bandit camps |
| 9 | Quarantine zone | §2, §6 | Special infected, bosses |
| 10 | NIGHTFALL research facility | §2, §5 | Story climax content |
| 11 | Other underground locations | §2 | Underground voxel structures |

## 9. Unresolved decisions
1. Area list, order, sizes and map positions (§8): review the candidates.
2. Bunker surface boundary and map position (§7.1).
3. Optional bunker NPC roles and population (§7.3).
4. Bunker story items, texts and placement (§7.4).
5. Order of the post-D8 tracks (§6).
6. Audio sourcing and licensing; radio voice (actor vs TTS) (§5).
7. Who produces final models and animations; acceptance process for owner visual approval.
8. Reference hardware for performance sign-off (RTX 4070-class and a weaker PC).
9. Co-op scope per area (which behaviours are "required co-op behaviour").
