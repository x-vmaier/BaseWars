# Status and remaining work

Branch `feature/prompt-implementation`. Plan tasks T1-T13 are implemented and committed; T13b and T14-T17 are not started.
Order matters: **T13b -> T14 -> T15 -> T16 -> T17**. One task per session (see `CLAUDE.md`).

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
| T13 | Biome label, minimap trees, chat box raised, clan chat disabled outside a clan, build menu shifted, social + build open together, high-contrast bars |

## Not started

### T13b Maintainability refactor (do FIRST, before T14)

Goal: code a new contributor can read and extend. **No behavior changes.** Verify with a zero-warning build plus before/after screenshots of the same
scene (fixed `BW_CAM` / `BW_ZOOM`, same seed) for buildings, units, ships and effects; the images must look identical.

Do it in this order, one commit per step:

1. **Formatting baseline.** If `.clang-format` already exists, use it as is; otherwise add one (derive it from the dominant existing style; column limit ~120;
   make sure it does not produce the odd lambda indentation seen today). Format the whole tree in ONE commit that contains nothing else, so later diffs stay readable.
2. **Inventory and plan.** List every function over ~50 lines and every multi-line lambda pushed into a container
   (`grep -rn "push_back({" src | grep "\[=\|\[&"`). Show the plan and the proposed registry design for approval before step 3.
3. **Draw queue.** Replace the `items.push_back({sortY, [=, this]() { huge body }})` pattern in `src/rendering/` (buildings, units, ships, effects) with a
   `DrawItem{sortY, callable}` whose callable only calls a named method. Split per-type drawing into `drawXxx()` methods or one drawer per kind,
   registered in a table.
4. **Type registries (ECS-style).** One data table per family (buildings, units, ships, NPC types) holding stats, costs, gates, sprite and a factory
   that attaches the type's components, plus a reference to its drawer. Special behavior is expressed as components handled by systems that iterate views,
   not as per-type classes or `switch` chains. Remove the existing per-type `switch` chains in server systems and UI (the build menu and context panel
   are generated from the tables). Keep tables used by both sides in the shared layer (`Shared.hpp` / `Progression.hpp`); do not break the append-only
   protocol or bump `SAVE_MAGIC` unless the save layout changes. Replace manual dependent-state cleanup with `on_destroy` hooks where it simplifies the code.
5. **Comments.** Convert every file header from the `// one-line` after the guard to a Doxygen `@file/@brief` block before `#pragma once`.
   Everywhere else use plain `//` comments only where the *why* is non-obvious; remove comments that restate the code. No Doxygen tags outside the file header.
6. **Function splitting and blank-line grouping.** Split functions over ~50 lines into named helpers and group code with blank lines by logical step.
   The owner already added some blank lines by hand in places; review them and regroup logically, do not trust them. Work in disjoint file sets
   (parallel agents allowed per `CLAUDE.md`); the lead builds and commits per group.
7. **Extension recipes.** Fill the `<file>` placeholders in the "Extending the game" section of `ARCHITECTURE.md` with the real file names.
8. **Proof.** Add one small throwaway building type (and one NPC type) using only the documented recipe, confirm it appears in the build menu,
   works, and renders. Record how many files it needed, then remove it (or keep it if the owner wants it). If the recipe needs more than ~4 files
   or any `switch` edit, the registry is not done.

Acceptance: zero warnings; screenshots identical to before; every file starts with the Doxygen block and no other Doxygen tags exist in the source
(`grep -L '@file' $(git ls-files 'src/*.hpp' 'src/*.cpp')` prints nothing; `grep -rn '@brief\|@param' src` shows only file headers); no multi-line lambdas
pushed into containers; no function over ~50 lines (or an explicit, short justification in the plan).

### T14 Naval overhaul

* Harbours produce standalone ships (queue + cost rows); a ship cannot move without crew aboard.
* Troop-focused and cargo-focused ship types, larger tiers at higher harbour levels with their own speed / troop / cargo capacity; timed, paid ship upgrades.
  Replace `SHIP_TROOP_CAP` / `SHIP_RES_CAP` (3 tiers in `Shared.hpp`) with an entry set in the T13b ship registry.
* Ship combat only with archers aboard or a crossbow upgrade.
* Wind direction (server-authoritative, replicated) + wind-rose speed modifier per heading, compass in the HUD.
* Grid-based Harbour window in the Archery (`TrainingPanel`) style: build ships / fleet / harbour upgrade.
* Multi-modal route planning (land -> boat -> land) for pawns and clan defense-quota troops whenever water is faster (generalise `FerryPlanner`).

### T15 Unit behavior and logistics

* Auto base defense vs manual target priority (manual orders must never be overridden by auto-acquire).
* Reliable automated supply caravans (including across water and to clan mates).
* Clean town relocation / donations / respawn (jobs, ships, caravans referencing a moved or destroyed Town Hall).

### T16 Visuals and controls

* Dynamic rain with a shader across biomes (lower in deserts); refine the ocean wave shader (`assets/shaders/water.fs`).
* Central beginner hint system (hints are currently scattered inline strings; collect them into one table).
* Pause-menu keybind screen: customizable and saved bindings (all keys are hard-coded `IsKey*` in `src/client/Input.cpp`).

### T17 Cleanup

* Remove the leftover dead code paths mentioned below, run a final warning pass, and a style pass over any file T13b did not cover.

## Where to start in the code

* **T13b:** draw queues in `src/rendering/` (building, unit, ship and effect renderers); per-type `switch` chains in `src/systems/`, `src/ui/BuildMenu`,
  `src/ui/ContextPanel`, `src/ui/TrainingPanel`; type tables in `src/common/Shared.hpp` (`KINFO`) and `Progression.hpp`.
* **T14:** `src/systems/NavalSystem.*` (ships, boarding, voyages), `FerryPlanner.*` (planTransport/startTransport; used by pawns' haul and
  `DefenseSystem::planDefenseRoute`), `src/components/Naval.hpp` (Ship/Aboard/Boarding/Voyage), ship constants in `src/common/Shared.hpp`,
  harbour gates in `Progression.hpp` (`BUILD_GATE`, `unitThLevel`), `TrainingSystem` (queue + `canTrain`), client `src/ui/ShipPanel.cpp`,
  `src/ui/TrainingPanel.*` (Archery window = template for the Harbour window; frame `assets/ui/training_panel.tmj`, generated by `tools/gen_ui_maps.py`),
  `src/rendering/ShipRenderer.*`. Replicated ship state rides `S_State` (`Info.level`=tier, `prog`=troops, `stock`=cargo) and `S_ShipInfo`.
* **T15:** `CombatSystem::fight`, `UnitSystem`, `DefenseSystem`, `ConvoyHaulSystem` (Haul/Convoy; see the reasoning comment block on the earlier loop bug),
  `SpawnSystem::spawnBase` / `NetSession` (respawn), `DefenseSystem` + `World::removeOwner` (cleanup), `ConstructionSystem`/`ClearingSystem` (jobs referencing owners).
* **T16:** shaders in `assets/shaders` (`water.fs`, `post.fs`; loader `src/rendering/Shaders.cpp`, `WaterRenderer`, `PostFx`), no weather system exists yet
  (add `src/rendering/WeatherRenderer` + a biome rain factor; weather can be client-derived from a replicated seed or server-driven like the planned wind);
  hints are inline strings in `ui/`; keybinds: `src/client/Input.cpp` + `Settings.hpp` (persisted in `settings.txt`) + `PauseMenu`.

## Known gaps / follow-ups from the finished work

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
