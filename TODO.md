# Status and remaining work

Branch `feature/prompt-implementation`. Plan tasks T1-T13b are implemented and committed; everything below "Roadmap" is not started.
**Do the first unfinished task in the Roadmap order, not the numerically lowest id** (ids are stable names; the order was re-planned after T13b).
One task per session (see `CLAUDE.md`).

## Done

| Task | Summary |
| --- | --- |
| T1 | `cmake/` modules, C++23, compile commands, mandatory Steamworks SDK with runtime copy, only `server` + `client` targets |
| T2 | Server split into `World` + 23 system classes + components; client split into App/Session/Input/... + `rendering/` + `ui/`; no `.inc`, no monolith files |
| T3 | ENet, P2P and the bots are gone; Steam GameServer + Networking Sockets (IP socket + SDR P2P socket). Direct-IP connect verified end to end |
| T4 | `config/server.cfg` (biomes, feature density, towns, hostile %, raids, patrols, sheep, instant debug timers) |
| T5 | Oval under buildings removed; every obstacle hoverable and right-click clearable; pawn walks, timer runs on site, drops, pawn resumes old work; placement over obstacles queues clear jobs |
| T6 | Faction patrols wander/siege hostile towns (+ faction wars, map-wide patrol groups); client keeps last-seen NPC town buildings dimmed |
| T7 | Convoy/haul loop fixed (hall-room check, cooldowns, no ash ghosts) + fire attribution fixes |
| T8 | Pawn-driven construction/upgrade/repair with timers that only run on site; production/training blocked meanwhile; visual indicators |
| T9 | Damaged-building efficiency, disabled when critical, food costs on actions |
| T10 | Level cap 10, steep cost/time curves, Town Hall gating table (`Progression.hpp`), Outpost TH3 / Harbour TH4 |
| T11 | Donation cooldown, shifting faction demand, diminishing gift returns |
| T12 | `ui::CostRow` (icon left, number right) everywhere; tabbed Town Hall window (Main / Storage / Clan with per-type defense quotas) |
| T13b | Maintainability refactor: `.clang-format`, draw queue with named drawers, kind registry (`common/KindDefs.hpp`: table + `KT_*` tags, `systems/KindFactories.cpp`, art tables in `rendering/`), Doxygen file headers, functions split, `Shared.hpp` split. Proof: a new building + troop needed 2 files (`KindDefs.hpp`, `rendering/UnitAnims.cpp`), 0 `switch` edits |
| T13 | Biome label, minimap trees, chat box raised, clan chat disabled outside a clan, build menu shifted, social + build open together, high-contrast bars |

## Roadmap (do in this order)

| # | Task | Why here |
| --- | --- | --- |
| 1 | **T18** Identity, defeat flow, tree occlusion | Bugs players hit today (wrong town on account switch, stuck defeat). Identity must be fixed before any new per-player state is saved. |
| 2 | **T19** Networking: tick snapshots, interpolation, prediction | Every later feature adds replicated state; build it on the new snapshot / command-sequence protocol (one deliberate protocol break, done once). |
| 3 | **T14** Naval overhaul | Ships, wind, harbour window; sea events (T22) and map icons (T21) build on it. |
| 4 | **T15** Unit behavior, logistics, pawn anti-tedium | Auto-defense vs manual orders, caravans, relocation cleanup, plus Clear Zone and pawn roles. |
| 5 | **T20** Combat triangle, garrisons, offline protection, ruins | Combat data model that the enemy ecology (T23) extends. Ruins reuse the T18 ruin component. |
| 6 | **T21** Parchment world map and minimap | Needs ships (T14), convoy routes (T15) and territory data to draw. |
| 7 | **T16** Visuals and controls (rain, hints, keybinds) | Independent polish. |
| 8 | **T22** Mid-game activities and sea events | Needs T14 (ships), T15 (caravans), T19 (cheap replication). |
| 9 | **T23** Tactical command, enemy ecology, stealth, game juice | Largest; split in phases 23a-23f. Needs the Enemy Pack assets (see its gate). |
| 10 | **T17** Cleanup | Always last. |

### Rules for every task below (from the owner's requirements)

* The server stays authoritative for all simulation state. The client only renders, predicts (T19) and sends commands.
* New buildings, units, NPCs, ships and enemies register through the registries: a `KINDS[]` entry in `src/common/KindDefs.hpp` (+ `KT_*` tag if it is a new
  behaviour flag), hooks in `src/systems/KindFactories.cpp`, drawers / art in `src/rendering/` (see "Extending the game" in `ARCHITECTURE.md`). No `switch` on kinds.
* Every tuning value goes into `ServerConfig` + `config/server.cfg` (documented, default reproduces the described behaviour). Shared rules go in `Progression.hpp`.
* New persistent state is written and read in `SaveSystem` and bumps `SAVE_MAGIC`; list in the task what is saved.
* Protocol is append-only; T19 is the one planned exception (documented there).
* New env test hooks are listed in `ARCHITECTURE.md`; each task is verified with a scripted run and screenshot (or a logged server check) and `TODO.md` lists what was only read.
* Art: only the Tiny Swords assets in `assets/` (no invented sprites; see the asset note in T23). Generated map / UI art for T21 is the one allowed addition.

## Not started

### T18 Identity, defeat flow and tree occlusion (do first)

**A. Strict SteamID ownership and persona-name lock (critical bug).**
Today a profile is matched by a client-generated `uuid` (`Session.cpp` reads / writes it in the local profile file, `NetSession::onHello` compares `pl[i].uuid`),
the client also sends its own name (`MainMenu` name box, `Settings.name`, `myName`) and an unauthenticated `steamId` in `C_Hello`. Switching Steam accounts on one
machine reuses the local uuid, so account 2 inherits and renames account 1's town.
* Bind player accounts, town ownership and save-file owner mappings only to the authenticated 64-bit SteamID (`CSteamID::ConvertToUint64()`) verified during the
  Steam session handshake (e.g. auth ticket: client `ISteamUser::GetAuthSessionTicket`, server `ISteamGameServer::BeginAuthSession`, or the connection identity
  `SteamNetConnectionInfo_t::m_identityRemote` for SDR). Never use machine ids, local config files, IP addresses or connection slots.
* A SteamID reconnecting from any machine loads exactly its own town; `acc2` never overwrites or duplicates `acc1`'s town.
* Remove the uuid from matching (`Profile.uuid`, `C_Hello`, the local `profile` file) and migrate saves by SteamID (bump `SAVE_MAGIC`; decide in the plan what happens to
  old uuid-only profiles).
* Remove every way to edit the player name: main-menu name box, `Settings.name`, `config.txt` / profile `name=`, any protocol field that carries a client-chosen name.
  Names always come from Steam (`SteamFriends()->GetPersonaName()` for the local user, `GetFriendPersonaName()` for others) and are refreshed on every login so the
  town label updates cleanly without touching any other SteamID's town. Decide in the plan how the server obtains the name (the dedicated server cannot call
  `ISteamFriends` for strangers).
* `--local` / direct-IP dev mode has no Steam identity: define an explicit dev-only fallback (documented flag / env hook) and keep the real path strict.
* Test: two Steam accounts (or two fake ids in dev mode) on one machine each get their own town and name; reconnect keeps it.

**B. Defeat state, burning ruin and resettlement flow.** Fixes the bug where a defeated player keeps the endless damage post-process (`PostFx` damage vignette),
the town vanishes instantly, the camera pans freely and no Town Hall can be placed (see also the respawn path in `NetSession::respawnIfDestroyed` and
`homeless` handling).
* Stop the endless damage post-process loop cleanly on defeat (find what keeps re-triggering it).
* The defeated town stays in place burning as a ruin / wreckage and disappears only after a configurable real-world duration (`defeat_ruin_hours`, default 24;
  real time, so it must survive server restarts; persisted). Implement it with a shared `Ruin` component (expiry, burning flag) that T20 reuses for destroyed buildings.
* Defeat banner / popup in the existing theme (paper banner, ribbon): names the conquering player (the last attacker, `Hit` component) and the conqueror's clan name if
  any, plus a theme-fitting button ("Resettle" / "Restart"; the owner's text says "Reseddle", treat as a typo).
* On click the server picks a random valid candidate site with strict priority: near a Steam friend -> near a clan member -> near any other player (reuse
  `SpawnSystem` clearing checks and `SPAWN_MIN_DIST`). The client enters a dedicated resettlement preview state where only **Accept** and **Resettle** work (each
  Resettle click asks the server for a new candidate); every other command and all build options are locked (server enforces it, not just the UI).
* Only **Accept** places the new Town Hall at the offered site and starts the fresh base. Persist the pending offer / defeated state in the save.
* Test hooks to add: force defeat of the local player, force the candidate priority cases.

**C. Tree canopy occlusion transparency** (`FeatureRenderer`, `src/rendering/`).
* A tree (pine / oak) fades to semi-transparent while any of the local player's own units (`owner == me`: pawns, warriors, archers, monks, lancers) stands behind its
  canopy / sprite bounds, with a smooth fade in and out (per-tree alpha state keyed by tile, decays when nothing is occluded).
* Wild animals, enemy units and NPC faction units never trigger it; trees stay fully opaque when no own unit is occluded.
* Must not change click picking or draw order. Test with a screenshot of a pawn behind a tree before / after.

### T19 Networking refactor: fixed timestep, snapshots, interpolation, prediction

Goal: no remote-internet input lag and no stop-and-start unit stutter, following Gambetta's Fast-Paced Multiplayer (client-server, prediction + reconciliation, entity
interpolation) for an open-world RTS on Steam Networking Sockets. First inspect `src/server/ServerMain.cpp` (tick loop), `src/networking/Steam*Net.*`
(everything is sent `k_nSteamNetworkingSend_Reliable` today), `src/systems/Replication.cpp` (`sendState` every 2nd tick = 10 Hz, paths every 8th),
`src/client/PacketHandler.cpp` (`onState`) and `ClientWorld::interpolate` (a simple exponential chase of `RPos` towards `Goal`, which is what stutters).
1. **Server:** fixed simulation timestep (`TICK_HZ`, make it a `server.cfg` value if safe for the tick-based timers), a command queue processed in fixed `dt`
   steps, tick-numbered snapshots. Frequent state uses unreliable / sequenced sends (keep each message under ~1200 bytes or split by interest area / chunk, because an
   unreliable message that fragments is lost if any fragment is lost); reliable only for one-off events (chat, effects that must not drop, joins, defeat, contracts).
   Snapshot rate is a `server.cfg` value.
2. **Client entity interpolation:** per-entity ring buffer of timestamped authoritative snapshots (client component), render time =
   `estimatedServerTime - interpolationDelay` (100-150 ms or ~2x the snapshot interval; client setting), interpolate between the two bracketing snapshots every frame,
   short velocity-clamped extrapolation only on gaps, never freeze and restart. Snapshots carry velocity, heading / facing and action state (idle, moving, attacking)
   so animations and stops stay in sync. Needs a server-time estimate (tick + RTT from `GetConnectionRealTimeStatus` / ping).
3. **Client prediction + server reconciliation** for the local player's commands (move / attack orders, building placement, ability triggers): monotonically increasing
   sequence number on every client command, immediate optimistic local feedback, pending queue of unacknowledged commands, `lastProcessedInputSeq` per client in state
   updates, on update drop acknowledged commands, reconcile to the authoritative state and replay the rest without rubber-banding. The server stays authoritative for
   combat, health, collisions, pathfinding and resource validation (decide in the plan what exactly is predicted for RTS orders: order markers, local movement start).
* This is the one planned protocol break: add the command sequence envelope and snapshot header at the end of `Msg`, bump a protocol version in `C_Hello` /
  `S_Welcome`, document it in `ARCHITECTURE.md`. Saves are unaffected.
* Test hooks: simulated latency / loss (`SteamNetworkingUtils` fake lag / loss config values, exposed as an env hook), a frame-time stutter metric in a scripted run.
* Verify with a throttled connection (not only localhost); update the "Known gaps" list (SDR is still untested).

### T14 Naval overhaul

* Harbours produce standalone ships (queue + cost rows); a ship cannot move without crew aboard.
* Troop-focused and cargo-focused ship types, larger tiers at higher harbour levels with their own speed / troop / cargo capacity; timed, paid ship upgrades.
  Replace `SHIP_TROOP_CAP` / `SHIP_RES_CAP` (3 tiers in `Shared.hpp`) with ship entries in `common/KindDefs.hpp`.
* Ship combat only with archers aboard or a crossbow upgrade.
* Wind direction (server-authoritative, replicated) + wind-rose speed modifier per heading, compass in the HUD.
* Grid-based Harbour window in the Archery (`TrainingPanel`) style: build ships / fleet / harbour upgrade.
* Multi-modal route planning (land -> boat -> land) for pawns and clan defense-quota troops whenever water is faster (generalise `FerryPlanner`).

### T15 Unit behavior and logistics

* Auto base defense vs manual target priority (manual orders must never be overridden by auto-acquire).
* Reliable automated supply caravans (including across water and to clan mates).
* Clean town relocation / donations / respawn (jobs, ships, caravans referencing a moved or destroyed Town Hall).
* **Pawn anti-tedium: Clear Zone.** Drag-select area tool next to the single-obstacle right-click clear: designates every tree / rock / gold vein in a rectangle at
  once (server expands the rect with a cap `clear_zone_max_tiles`; reuse `C_Clear` / `ClearingSystem` jobs, appended message if needed).
* **Pawn job priorities and roles.** Roles Builder/Repair, Obstacle Clearer, Logistics Hauler as a persisted per-pawn component, set per pawn (context panel) and as a
  Town Hall UI allocation. The scheduler (`ConstructionSystem` / `ClearingSystem` free-pawn choice, `ConvoyHaulSystem`) honours priorities so critical repairs and
  construction are never starved by distant clearing (see the known gap "clearing jobs and construction compete for the same pawns"). Defaults in `server.cfg`.

### T20 Combat triangle, garrisons, offline protection and ruins

* **Counter triangle (data in `KindDefs`, no per-type code).** Melee infantry (warrior: high arrow resistance, screens the front line), ranged / fire archers (high damage
  against unarmored units and structures), siege / raider units (bonus damage against walls and buildings, vulnerable to infantry). Replace the hard-coded `damageVs` /
  `armorOf` in `Shared.hpp` by role / armor-class fields in `KindDef` plus a damage matrix whose factors are `server.cfg` values. No new sprites: realise the siege
  role with existing units (e.g. faction raiders) and decide in the plan.
* **Defensive structures and garrisoning.** Walls and watchtowers (the asset pack has no wall sprite: reuse `Tower` for watchtowers, decide in the plan how walls look).
  New `Garrison` component (capacity, occupants, hidden like `Aboard`), tag for garrisonable buildings, appended commands to garrison / leave. Garrisoned ranged units get
  more range, damage and protection; base layout and choke points decide raids. Replicate occupants; save garrisons.
* **Offline raid protection (config).** When the player and all online clanmates are offline: reduced incoming structural damage, NPC faction sieges against the base
  paused (`PatrolSystem` / `FactionSystem` targeting), and a configurable temporary peace shield after heavy destruction (threshold and duration in `server.cfg`,
  real-time, persisted, shown in the HUD, ended by the owner attacking someone). Never block the owner's own play.
* **Scorched blueprint ruins.** When a player building is destroyed it leaves a ruin on its tile (shared `Ruin` component from T18, rendered as a dark scorched ghost of the
  sprite) that can be rebuilt at significantly reduced resource cost and construction time (`ruin_rebuild_cost_pct`, `ruin_rebuild_time_pct`); hook it into
  `World::destroyNow` / footprint handling and the build / placement checks; persisted with an expiry.

### T21 Hand-drawn parchment world map and minimap

Current code: `src/ui/BigMap.*` (full screen map, opened by clicking the minimap or `N`), `src/rendering/Minimap.*`, `src/rendering/MapOverlay.*` (icons shared by both).
Redesign into an authentic medieval parchment map; keep it crisp, readable and fast.
* **Look:** warm brown aged-parchment background (paper grain, vignette), dark charcoal / near-black ink linework and ink icon assets. Generated PNGs are allowed
  (script in `tools/`, output under `assets/ui/`, like `gen_ui_maps.py`). No flat biome color fills and no single-pixel unit dots.
* **Biomes as ink motifs with light shading:** forests = scattered ink trees / canopy clusters; mountains = craggy peaks and ridge hatching; hills and plains = rolling
  contour strokes and grass tufts; deserts = wind-rippled dune lines and sparse scrub; wetlands / marshes = reed and water-tuft strokes (only if the world has them: check
  the `Biome` enum, else map the closest biome); coastlines = crisp ink shore contours.
* **Ocean:** hand-drawn dark-ink wave lines that drift, ripple and undulate smoothly over time. Keep it performant: deterministic motif placement from `hash2(x, y, seed)`,
  static layers cached per chunk in render textures (rebuilt only when explored tiles change), the ocean animated by a phase offset (pre-baked wave strips or a small shader
  loaded like `Shaders.cpp`).
* **Icons:** distinct hand-drawn ink icons for towns, outposts, harbours, ships and troop groups. Omit or aggregate individual pawns (cluster icon with a count) so the map
  stays legible.
* **Borders:** claimed player, clan and hostile faction territory as dashed ink border lines with different dash styles for friendly / clan / enemy.
* **Routes:** active supply convoys, trade caravans and support patrols as dashed ink travel lines with arrowheads (data already replicated for the map: convoy / patrol
  paths, `S_ShipInfo` path lines; check `MapOverlay`).
* **Custom markers:** place, label and remove hand-drawn markers (warning skull, attack swords, resource icons, anchor pin) by right-click or a compact ink toolbar; personal
  markers persist locally (client file next to `settings.txt`), tactical markers can be shared with clan members (appended protocol messages, relayed by the server to the
  clan, length-capped labels, rate-limited).
* **Minimap:** miniature parchment with an ink border, same icon set, no HUD overlap (fixes the known gap: minimap overlapping the resource bar at UI scale 2 in small
  windows) and smooth interaction.
* Test: screenshots of the big map at several zoom levels and of the minimap with the build menu and chat open.

### T16 Visuals and controls

* Dynamic rain with a shader across biomes (lower in deserts); refine the ocean wave shader (`assets/shaders/water.fs`).
* Central beginner hint system (hints are currently scattered inline strings; collect them into one table).
* Pause-menu keybind screen: customizable and saved bindings (all keys are hard-coded `IsKey*` in `src/client/Input.cpp`).

### T22 Mid-game activities and sea events

* **Contracts and mini-quests from NPC towns** (server system + persisted `Contract` state): escort merchant carts, clear nearby bandit camps, deliver time-sensitive
  supply orders. Rewards: instant build / upgrade speed-up tokens (a per-profile counter consumed by a context-panel action that shortens the running `BJob`), gold or rare
  resources (do not add a `Res` enumerator lightly: it sizes many arrays; prefer counters). UI: contract list in the NPC town context panel + a small HUD tracker. Reuse
  existing units / sprites (carts = pawns / existing convoy, bandits = black faction units).
* **World discoveries:** hidden treasure caches, ancient ruins and rare wildlife herds (tinted sheep) that reward scouting and hunting parties; spawn density in `server.cfg`.
* **Sea events** (needs T14): salvageable shipwrecks and flotsam, wandering pirate patrol ships that drop loot, fishing shoals for food (a ship works the shoal),
  and small remote islands (`genBiome` already makes rare islands) with rich gold / rare ore veins (new feature type, existing gold sprites) that reward distant outposts and
  automated cargo ferry routes (T15 caravans, `FerryPlanner`).
* All spawns, timers and rewards are server-side, configurable, persisted and appear on the world map (T21).

### T23 Tactical command, enemy ecology, stealth and game juice

**Asset gate (read first).** The design is limited to Pixel Frog's Tiny Swords (64x64 grid, 10 fps base animations, 5 faction colors blue / red / purple / yellow / black,
elevation, stairs, water, animated bushes / rocks, VFX and UI). `assets/` currently has only the Free Pack (units and buildings in 5 colors, terrain, particles: Dust x2,
Fire x3, Explosion x2, Water Splash, UI: banners, bars, buttons, cursors, icons, ribbons, swords, wood table). **The Enemy Pack (Stone Turtle, Boar, Minotaur,
Boompuff, Torch Goblin, Slingshot Gnome, Hex Goblin, Giant Bat, Bumblebee, Imp ...) is not in the repo and there is no audio.** Phases 23c-23e need the owner to add the Enemy
Pack (license permitting); never invent sprites. Everything else can start with the existing assets. Open design decision: tactical slow-motion vs an authoritative
multiplayer server (see 23a).
* **23a Command Focus ("god mode" control).** Slow time to ~15% while clicking / dragging units or holding Space, with command lines, range rings and target previews.
  The sim is server-authoritative and shared: decide in the plan between (a) client-side presentation slow-motion with orders previewed and buffered while the server keeps its
  tick, and (b) a real server time scale only when no other player is affected. Not both silently.
* **23b Anti-punishment flow.** No permanent wipe of hours of progress from one mistake; defeated squads retreat wounded to camp and are healed by Monks / Meat; partial clears
  grant permanent meta-resources, blueprint unlocks or tactical perks (persisted per profile).
* **23c "Learn and counter" enemy ecology** (needs the Enemy Pack). Each archetype is a `KindDef` entry + components (`Charger`, `Volatile`, `Igniter`, `Ranged`,
  `Disruptor`) + systems; every threat has a visible telegraph (wind-up flash, ground danger zone, icon badge from the UI icons), never unfair trial and error.
  Player stances as components: Lancers *Hold*, Warriors *Guard / Bash*.
  * Armored / heavy chargers (Stone Turtle, Boar, Minotaur): countered by Lancers in Hold stance on narrow Stairs (halts the charge, stuns).
  * Volatile and ranged (Boompuff, Torch Goblin, Slingshot Gnome): Torch Goblins ignite grass / buildings with the 3 Fire effects (`FireSystem`); Boompuffs explode on death.
    Counters: snipe early with elevated Archers, shove into enemy ranks with Warrior Guard / Bash, or knock off cliffs into water (Water Splash = instant kill).
  * Disruptors and swarmers (Hex Goblin, Giant Bat, Bumblebee, Imp): fast flyers that bypass stairs and elevation; counter with Warriors in Guard stance shielding the Monks.
* **23d Stealth, reconnaissance and two-way raids.** The 4 animated Bushes and elevation cliffs give stealth: units inside bushes or behind cliffs become semi-transparent with a
  UI indicator (server-side `Stealth` visibility rules in `Replication`, not only a client fade); sneaking Archers onto high ground or flanking behind patrol cones (FOV cones for
  `PatrolSystem`) trigger **Ambush Crits** (bonus damage, dust burst, hitstop). Offensive raids on Red / Purple / Yellow / Black faction camps (pick off lookouts, burn supply huts,
  rescue captured Pawns, extract with loot) and base defense using elevation choke points and traps (traps built only from existing VFX / buildings).
* **23e Micro-tasks and Golden Opportunities.** Keep pawns useful mid-fight (mine a gleaming Gold rock before enemies arrive, herd / harvest Sheep for healing Meat, repair a
  burning Defense Tower) and add optional time-limited events with a countdown (intercept a fleeing Gold-carrier Imp within 25 s, hold a cliff shrine 20 s for a permanent squad
  blessing, extinguish a burning barracks before expiry for double Wood). Failure never kills the player; success pays out. Server-side `Opportunity` entities, persisted.
* **23f Game juice.** Mostly client presentation driven by server fx events: hitstop / freeze frames (40-80 ms) on heavy hits, crits, kills and parries, directional
  knockback, camera shake (`PostFx` shake exists), white hit-flash shader (extend the sprite shaders in `assets/shaders`), squash-and-stretch on 10 fps sprites, floating damage and
  loot numbers (`EffectsRenderer` drops are the base), escalating pitch chimes (**no audio system exists yet**: add one plus CC0 / generated sounds, flag the licensing),
  magnetic resource pickups flying into the banner, stretchable Ribbon combo callouts ("CLIFF DROP! +50 Gold", "AMBUSH!") from the UI ribbons in 5 colors, existing VFX combined.

### T17 Cleanup

* Remove leftover dead code paths, finish the T13b leftovers listed under "Known gaps", run a final warning pass, and a style pass over any file T13b did not cover.

## Where to start in the code

* **T14:** `src/systems/NavalSystem.*` (ships, boarding, voyages), `FerryPlanner.*` (planTransport/startTransport; used by pawns' haul and
  `DefenseSystem::planDefenseRoute`), `src/components/Naval.hpp` (Ship/Aboard/Boarding/Voyage), ship constants in `src/common/Shared.hpp`,
  harbour gates and ship entries in `common/KindDefs.hpp` (`gate`, `thLevel`) with the rules in `Progression.hpp`, `TrainingSystem` (queue + `canTrain`), client `src/ui/ShipPanel.cpp`,
  `src/ui/TrainingPanel.*` (Archery window = template for the Harbour window; frame `assets/ui/training_panel.tmj`, generated by `tools/gen_ui_maps.py`),
  `src/rendering/ShipRenderer.*`. Replicated ship state rides `S_State` (`Info.level`=tier, `prog`=troops, `stock`=cargo) and `S_ShipInfo`.
* **T15:** `CombatSystem::fight`, `UnitSystem`, `DefenseSystem`, `ConvoyHaulSystem` (Haul/Convoy; see the reasoning comment block on the earlier loop bug),
  `SpawnSystem::spawnBase` / `NetSession` (respawn), `DefenseSystem` + `World::removeOwner` (cleanup), `ConstructionSystem`/`ClearingSystem` (jobs referencing owners).
* **T16:** shaders in `assets/shaders` (`water.fs`, `post.fs`; loader `src/rendering/Shaders.cpp`, `WaterRenderer`, `PostFx`), no weather system exists yet
  (add `src/rendering/WeatherRenderer` + a biome rain factor; weather can be client-derived from a replicated seed or server-driven like the planned wind);
  hints are inline strings in `ui/`; keybinds: `src/client/Input.cpp` + `Settings.hpp` (persisted in `settings.txt`) + `PauseMenu`.

* **T18:** `src/systems/NetSession.*` (`onHello`, `claimSlot`, `respawnIfDestroyed`), `src/client/Session.cpp` (uuid / name profile file) and `Settings.hpp`, `src/ui/MainMenu.cpp`
  (name box), `src/networking/Steam*Net.*` (connection identity), `src/networking/SteamClient.*`, `SaveSystem` (`Profile.uuid`, `steamId`), `SpawnSystem` (spawn site logic),
  `src/rendering/PostFx.cpp` (damage), `src/ui/BaseMenus.*` (homeless / placement banners), `FeatureRenderer` + `ClientWorld::reg` (tree occlusion).
* **T19:** `ServerMain.cpp` (tick loop, send cadence), `Replication.cpp`, `Steam*Net.*`, `client/PacketHandler.cpp` (`onState`), `ClientWorld::interpolate`, `client/Commands.*`.
* **T20:** `Shared.hpp` (`damageVs`, `armorOf`), `CombatSystem`, `FactionSystem` / `PatrolSystem` (sieges), `World::destroyNow`, `components/Naval.hpp` (`Aboard` is the model for garrisons).
* **T21:** `ui/BigMap.*`, `rendering/Minimap.*`, `rendering/MapOverlay.*`, `rendering/Shaders.*`, `tools/gen_ui_maps.py` (generator pattern), `client/Settings.hpp` (local persistence).
* **T22:** `TownSystem` / `FactionSystem` (NPC towns), `HuntSystem` (sheep), `ClearingSystem`, `FerryPlanner`, `WorldGen.hpp` (islands, features).
* **T23:** `FireSystem`, `CombatSystem`, `PatrolSystem`, `Replication.cpp` (visibility), `EffectsRenderer` / `FxDrawers.cpp`, `PostFx.cpp`, UI icons / ribbons under `assets/UI Elements`.

## Known gaps / follow-ups from the finished work

* **T13b leftovers:** `onPacket` (75 lines, a flat message dispatch), `SaveSystem::readBaseEntity` (69, field order), `NetSession::onHello` (62), `App::run` (57) and
  `Input::update` (51) are over ~50 lines; `World.cpp`, `SaveSystem.cpp`, `ClearingSystem.cpp`, `systems/PacketHandler.cpp` are 650-720 lines.
  A new troop beyond the five existing ones also needs a card in `TrainingPanel` (fixed 5 cards) and unit-level replication (`K_Pawn..K_Monk`); small kind checks
  remain in `Input.cpp` (hit radius), archer fire arrows and the patrol / raid rosters.
* **T13b verification:** before/after screenshots were compared on one coastal scene (same window size): static pixels, resource, unit and building counts matched; only moving
  units and foliage differ (as they do between two runs of the old build). Not compared: raids, combat, save / reload, the Outpost (placement failed in the scene), ship rendering
  (the two ships were trained but not visible in the frame). Agents' function splits were verified by build + that scene only.

* **Not play-tested in real sessions.** Only short scripted runs with screenshots. Never exercised: save/reload of construction jobs, cancel refund,
  training-queue pausing, efficiency numbers in production/defense, demand rotation, gift diminishing returns, per-type defense dispatch in a real
  attack, discarded income above a storage share, the clan-leave confirm click, harbour ship training through the context panel.
* **Steam Datagram Relay and the server browser are untested** (need a published app id; `steam_appid.txt` is the Spacewar test id 480). Only direct IP was run.
* The client segfaults inside `steamclient.so` on exit after a screenshot run (predates the refactor); roughly 1 in 3 scripted client runs time out.
* **Behaviour changes to review:** `patrol_map_groups` now defaults to 2 and `faction_war_percent` / `patrol_siege_*` keys changed the old defaults;
  build times x2 and production x0.6; starting meat 150 -> 200. Tune in `config/server.cfg` / `Progression.hpp`.
* `debug_instant_*` timers skip the pawn walk entirely.
* Clear jobs and pending builds are not saved. Relocation, town move and homeless Town Hall still need a pre-cleared site.
* Clear tooltip shows the base time (client does not know `clear_time_scale`). The working pawn's tool still depends on biome, not the obstacle.
* Clearing jobs and construction compete for the same pawns; a base with all pawns clearing makes upgrades wait.
* Pre-existing UI issues: at UI scale 2.0 in very small windows the minimap overlaps the resource bar and the build menu runs off the bottom.
* The biome label hides while the chat bar is open.
* Raised-chat / social-build keyboard toggles, and the greyed Clan-chat tooltip, were only verified by reading.
* Old saves (BWS6 and earlier) are intentionally discarded (moved to `.bad`); the format is now BWS8.
* The tiling WM gives the client a random window size, so `BW_WIN` is ignored; screenshots vary in size.
