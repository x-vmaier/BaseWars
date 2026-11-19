# Status and remaining work

Branch `feature/prompt-implementation`. Plan tasks T1-T13 are implemented and committed; T14-T17 are not started.

## Done
| Task | Summary |
|---|---|
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
### T14 Naval overhaul
* Harbours produce standalone ships (queue + cost rows); a ship cannot move without crew aboard.
* Troop-focused and cargo-focused ship types, larger tiers at higher harbour levels with their own speed / troop / cargo capacity; timed, paid ship upgrades.
  Replace `SHIP_TROOP_CAP` / `SHIP_RES_CAP` (3 tiers in `Shared.hpp`) with a shared data table.
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
* Central beginner hint system (hints are currently scattered inline strings).
* Pause-menu keybind screen: customizable and saved bindings (all keys are hard-coded `IsKey*` in `src/client/Input.cpp`).

### T17 Cleanup
* Remove the leftover dead code paths mentioned below, run a final warning pass.

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
