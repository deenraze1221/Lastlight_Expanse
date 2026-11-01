# LASTLIGHT EXPANSE

## Game Design Document --- v1.1 (Amendment 1)

> **Amendment 1 (2026-11-01, approved by the project owner):** final geometry direction
> (hybrid voxel environment + imported models, 0.5 m base grid, block definitions, shapes),
> final texture resolution standard, chunk/region definition, handcrafted world with
> procedural generation as an authoring tool. Changes are marked "(A1)" in §2 and §3.
> Technical specification: `docs/guide/VOXELS.md`.

### 1. Overview

-   Genre: Open-world PvE survival horror.
-   Setting: Zerovia, Eastern Europe, 2032; main city: Novograd.
-   First-person; solo or 1--4 player co-op (architecture should allow
    5).
-   Core loop: Prepare in bunker → take missions → explore → fight and
    loot → return → trade and store supplies.
-   Endless survival gameplay with repeatable missions.

### 2. World

-   Initial handcrafted map: 4 × 4 km (16 km²), expandable in future
    updates.
-   Chunk-based world: 64 × 64 m; dynamic loading, unloading and LOD.
    (A1) The 64 × 64 m unit is the world **region** (streaming, storage,
    authoring, future multiplayer synchronisation); each region contains
    4 × 4 internal meshing chunks of 16 × 16 × 16 m (32³ base cells).
-   (A1) The map stays handcrafted. Procedural generation (deterministic
    terrain, roads, rivers, stamped structures) is an authoring tool whose
    result is edited by hand and saved as region data; persistent world
    changes are stored per region. No endless or randomly generated runtime
    world.
-   Regions: city, suburbs, forests, villages, industrial areas,
    military sites, quarantine zones, research facilities and
    underground locations.
-   Mostly flat terrain with gentle hills, rivers, lakes, streams,
    wetlands and bridges.
-   Vehicles are static environmental props; no driving.
-   Shared exploration map with Fog of War, team positions, mission
    markers and custom markers.

### 3. Art Direction

-   Hybrid voxel/block-influenced geometry with natural silhouettes.
-   (A1) Environment from voxel blocks: walls, floors, ceilings,
    buildings, structural architecture, roads, terrain, rocks,
    landscape, bunkers, underground facilities and other large
    structures. Block construction resembles Minecraft; the graphics are
    far more atmospheric and sophisticated.
-   (A1) Base grid 0.5 m (configurable in the engine). Full Block
    1 × 1 × 1 m (2 × 2 × 2 cells); Half Block / Slab 1 × 1 × 0.5 m
    (2 × 2 × 1); Vertical Half Block 0.5 × 1 × 1 m (1 × 2 × 2); Sub-block
    0.5 × 0.5 × 0.5 m (one cell, 1/8 of a Full Block; not a "quarter
    block"). Extensible shapes: stairs, slopes and wedges, inner and
    outer corners, arches and curved shapes, thin panels, more later.
-   (A1) Voxel blocks: architecture and structural environment.
    Imported 3D models (glTF/GLB): weapons, characters, mutants,
    furniture, generators, machinery, computers, vehicles, equipment and
    decorative props. Blockbench is primary (voxel/low-poly), Blender
    for complex and organic models.
    Voxel geometry and models share style, materials, lighting and
    rendering.
-   (A1) Texture resolution standard (replaces "32×32 default, 64×64
    maximum"): 32×32 for small/simple details and assets; **64×64
    standard** for all environment textures, voxel blocks, architecture,
    furniture, props and equipment (64 texels per metre on voxel
    surfaces); 128×128 only for characters and weapons; 256×256 and
    above prohibited unless the project owner approves an exception.
    8×8 and 16×16 are not approved (invalid for new assets).
    Nearest-neighbor filtering.
-   Muted, faded colors and Eastern European post-war atmosphere.
-   PBR materials, realistic lighting and shadows; UE5-inspired
    atmospheric presentation.
-   Detailed environments/interiors, environmental storytelling, natural
    vegetation and realistic water.
-   Fog, rain, puddles, wind, volumetric lighting and smoke.

### 4. Time, Weather and World Simulation

-   Day/night: 30 minutes day + 30 minutes night, with smooth dawn and
    dusk.
-   Weather is independent of day/night and synchronized across players.
-   12 weather states: Clear, Partly Cloudy, Cloudy, Overcast, Drizzle,
    Light Rain, Moderate Rain, Haze, Fog, Dense Haze, Thunderstorm and
    Distant Lightning.
-   Weighted, context-aware weather transitions; no sudden purely random
    changes.
-   Weather affects visibility, lighting, fog, rain, wet surfaces,
    puddles, wind, ambience and thunder.
-   Wind moves vegetation; ambience includes birds, insects, creaking
    buildings, rain and distant activity.
-   District-level electricity: generators, repairable panels, emergency
    lights and restorable power in selected areas.
-   Wildlife: deer, wolves, wild boars, foxes, dogs and birds; natural
    habitats and reactions to players, noise and predators.
-   Every 48 real-time hours, refresh eligible ordinary loot, rotate
    selected rare loot, and rotate repeatable missions/events. Never
    reset story progress, player inventories, bunker state or permanent
    discoveries.

### 5. Story and Setting

-   A Third World War devastates Eastern Europe; Zerovia becomes
    isolated.
-   By 2032, Novograd is largely abandoned and infrastructure and
    communications have collapsed.
-   A classified military research project, NIGHTFALL, operated in the
    region. Its purpose and connection to the outbreak remain unknown.
-   Players begin in a shared underground bunker and investigate the
    region.
-   Storytelling is mainly through documents, notes, military reports,
    audio recordings, radio transmissions, computers and environmental
    clues.
-   Minimal mandatory dialogue and no reliance on long cutscenes.
-   The main story may conclude, but endless survival gameplay continues
    afterward.

### 6. AI, Enemies and Bandits

-   Target 8--12 enemy types: infected, fast/heavy variants, stealthy or
    jumping mutants, former military, mutated animals and rare special
    enemies/bosses.
-   Advanced AI: patrols, sound detection, searching, obstacle
    navigation, flanking, cover and group coordination.
-   Very few living NPCs outside the bunker; no large settlements.
-   Bandits appear in small, uncommon groups. They guard locations,
    patrol, ambush, investigate noise, search for players, coordinate,
    use cover and may retreat.
-   Wildlife has habitat-based routines and reacts to danger and player
    activity.

### 7. Weapons, Combat and Equipment

-   15--20 firearms and 5 melee weapons at launch.
-   Categories: pistols, SMGs, assault rifles, shotguns, sniper rifles
    and possibly machine guns.
-   Basic attachments only: sights, suppressors, flashlights and lasers.
-   Weapons differ in damage, recoil, fire rate, reload time, range and
    sound.
-   Four equipment tiers for helmets and armor; backpacks have four
    capacity tiers.
-   Armor has durability.

### 8. Inventory, Survival and Medicine

-   PUBG-style inventory: grid, item stacks, weight/capacity, backpack
    tiers, weapon/equipment slots and ground-loot interface.
-   Moderate hunger and thirst; food and water are found or purchased.
    Severe deprivation affects stamina and survival.
-   Medical system: health, bleeding, fractures, bandages, splints,
    medicine, antiseptics and teammate revival.
-   No crafting.
-   On death, all carried items drop at the death location and can be
    recovered. Player respawns in the bunker; level and XP remain.
-   Each player has a private locker in the shared bunker. Stored items
    are protected from death and other players.

### 9. Progression, Missions and Economy

-   Player levels use XP from missions, kills, exploration and
    cooperation.
-   Levels unlock access to weapons, equipment and items; no skills,
    perks or attribute trees.
-   Bunker merchants generate repeatable random missions from
    role-specific pools:
    -   Weapon dealer: weapons, ammunition and related supplies.
    -   Medic: medicine and medical supplies.
    -   Resource trader: food, water, fuel and materials.
    -   Technician: electronics, tools and machine parts.
    -   Scout: locations, signals, documents and reconnaissance.
-   Missions may repeat; unique story missions do not.
-   Hybrid economy: money for regular goods and barter for selected rare
    items/resources.
-   Merchants have specialized inventories and changing availability.

### 10. Bunker

-   One permanent shared team bunker; no construction, upgrades or room
    expansion.
-   Contains merchants, NPCs, private player lockers and common areas.
-   Safe hub for storage, trading, mission selection and expedition
    preparation.

### 11. Multiplayer and Saving

-   Host-based multiplayer: host opens a world and runs the local
    server.
-   Support LAN and direct IP/port; compatibility target includes
    Hamachi and Porthole.
-   Steam Friends invitations require Steamworks integration; adding the
    executable as a non-Steam game alone does not provide native
    invitations.
-   Shared world, missions, weather, time, loot and exploration;
    individual player levels, inventories and lockers.
-   Server-authoritative gameplay; design for 4 players first, with room
    for 5.
-   Autosave every 5 minutes and on normal host exit.
-   Save world state, player data, inventories, lockers, missions and
    world changes using atomic writes and backups.
-   When the host exits, save and close the session. Friends can rejoin
    when the host opens the world again.

### 12. Audio and Interface

-   Spatial 3D audio, surface-specific footsteps, indoor reverb,
    practical sound occlusion and directional enemy/wildlife sounds.
-   Rare atmospheric music; environmental sound is the main audio focus.
-   Tactical-minimalist HUD: health, stamina, hunger, thirst, ammo and
    important status indicators.
-   No minimap. Separate world map for Fog of War, team positions,
    mission markers and custom markers.

### 13. Technical Direction

-   Engine: ZODAR Engine; C++23 and Vulkan 1.3.
-   Modular reusable engine; separate generic engine systems from
    game-specific systems.
-   Windows PC target. Optimize for RTX 4070-class hardware while
    supporting weaker PCs.
-   Prioritize stable co-op, streaming, performance and reliable
    persistence.
-   Unspecified details remain open for later design decisions.
