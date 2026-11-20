# Base Wars - agent handoff

C++23 RTS (EnTT + Raylib + Steamworks). One authoritative `server`, one `client`. **Read `TODO.md` first** (status, the ordered Roadmap and the task details),
then `ARCHITECTURE.md` (layout, conventions, extension recipes, planned architecture, test hooks). Branch: `feature/prompt-implementation`. Work is committed through T13b.

## Session protocol (read this first)

1. Do **only the first unfinished task in the Roadmap table of `TODO.md`** (currently **T18**; task ids are stable names, the Roadmap order is what counts, e.g. T18 and T19 come
   before T14). Never start the next task in the same session.
2. Start in plan mode: read the relevant code, then show a short plan (design + file list + order) and wait for approval before editing.
3. Work in small steps. After each step: build warning-free, run the game, and verify the result (see "Definition of done").
4. Stop after the task. Update `TODO.md` (move it to Done, list what was only read-verified), update `ARCHITECTURE.md` if structure or extension points changed, commit, report.

## Build

* Steamworks SDK must exist in `third_party/steamworks` (git-ignored; it is on the dev machine). CMake fails without it.
* `cmake --preset release && cmake --build build/release` (or any build dir: `cmake -S . -B <dir> -G Ninja && ninja -C <dir>`).
  Both targets must build with **zero warnings**. After CMake changes re-run configure (sources are globbed with CONFIGURE_DEPENDS).
* The build dir gets `assets` (symlink), `config.txt`, `server.cfg`, `steam_appid.txt`, libsteam_api.

## Running the game (verified workflow)

* **The Steam client must be running** (`steam -silent &`, wait until `~/.steam/steam.pipe` is fresh and it has finished updating) or both exes fail
  in `SteamAPI_Init` / `SteamGameServer_Init` (the client prints the reason). The dev machine's Steam works once started.
* From the build dir: `./server 7777 42 mysave.bin Test --local &` (fresh save name each time; `--local` = no Steam backend login, direct IP only), then
  `BW_SHOT=out.png BW_SHOT_FRAMES=300 timeout 60 ./client --connect 127.0.0.1:7777` and view the PNG with the Read tool.
* Client hooks (`src/client/TestHooks.cpp`): `BW_UI`, `BW_SITE`, `BW_CAM`, `BW_ZOOM`, `BW_CMD`, `BW_MOUSE`, `BW_SCALE`, `BW_THTAB`... Server hooks and
  `debug_instant_*` timers are listed in `ARCHITECTURE.md` / `config/server.cfg`. `BW_NETLOG=1` traces Steam connection states.
* Known flakiness: ~1 in 3 scripted client runs time out without a screenshot (re-run); the client segfaults in `steamclient.so` at exit (ignore).

## Rules that bit us

* **Identity is the authenticated 64-bit SteamID** (planned fix in T18; until it lands the server still matches by `uuid`, do not extend that). Never key ownership, saves or
  names on machine ids, local files, IPs or connection slots, and never accept a player name from the client: names come from Steam.
* **Server authority:** the server owns all simulation state (combat, health, resources, pathfinding, timers, rewards, ruins, shields). The client renders, predicts (after T19) and
  sends commands. A feature that needs new state also needs: a component + system on the server, replication, a `SaveSystem` entry (bump `SAVE_MAGIC`), `server.cfg` tunables.
* **New content goes through the registries** (`KindDefs.hpp` entry + `KT_*` tag, `KindFactories.cpp` hooks, drawer / art tables). Never add a `switch` or `if (kind == ...)` chain.
* **Protocol:** append-only; T19 deliberately breaks it once (command sequence envelope, tick-numbered snapshots, protocol version). After T19 every client command carries a sequence
  number and every state message a tick; keep new messages inside that scheme.
* **Assets:** only the Tiny Swords Free Pack that is in `assets/` may be used (units / buildings in 5 colors, terrain, VFX, UI). The Enemy Pack and any audio are **not** in the
  repo; do not invent sprites or reference missing files. Generated map / UI art (T21) is the only exception, produced by a script in `tools/`.
* Screenshot runs: the tiling window manager gives the client a random window size per run, so compare before / after images only at the same size (re-run until equal) and expect
  moving units and foliage to differ between any two runs.

* Protocol is append-only: new `Msg` enumerators go at the END of the `C_*` / `S_*` group. Bump `SAVE_MAGIC` (`SaveSystem.hpp`) when the save layout changes.
* Tunables -> `ServerConfig` + `config/server.cfg` (documented); gate/cost/time rules -> `src/common/Progression.hpp` (shared by client and server).
* No `.inc` files, no monoliths (keep files < ~600 lines), header beside source, no extra executables/bots.
* Client UI: every resource cost goes through `ui::drawCosts` (`src/ui/CostRow`); buttons carry only a label. Use `ui::` screen/mouse helpers inside UI code.
* Do not run `pkill -f './server 7777'` in the same shell command line that contains that text (it kills its own shell); use `pkill -x server`.
* Parallel agents: split by disjoint files, and only one agent at a time may touch shared files (`Shared.hpp`, `World.*`, `PacketHandler.*`,
  `SaveSystem.*`, `ServerConfig.*`). Agents must not run git write commands; the lead builds, verifies and commits per task.
* Never claim a mechanic works unless it was run; `TODO.md` lists what was only verified by reading.

## Code style (mandatory for all new and touched code)

The goal is code a new contributor can read in one pass and extend without touching many files.

* **File header:** a Doxygen block as the very first thing in the file, BEFORE `#pragma once` and includes. One brief line, no essays.
  This is the ONLY place Doxygen is used:

  ```cpp
  /**
   * @file NavalSystem.hpp
   * @brief Ship movement, boarding and voyages.
   */
  #pragma once
  ```

* **Comments everywhere else:** plain `//` comments, no Doxygen tags (`@brief`, `@param`, `/** */`) on classes, functions or members.
  Comment the *why* only: a non-obvious rule, an invariant, a past bug, a unit or ownership that cannot be read from the code.
  Never restate what the code says. Self-explanatory functions get no comment. No per-line comments.
* **No large lambdas pushed into containers.** Never `items.push_back({y, [=, this]() { ...50 lines... }})`.
  A lambda in a draw queue, job list or callback is at most ~3 lines and only calls a named method:

  ```cpp
  items.push_back({sortY, [=, this] { drawBarracks(b); }});
  ```

  Real logic lives in named private methods or free functions.
* **Blank lines group logic.** Separate setup, computation, side effects and the return. Separate unrelated steps. Split any function longer than ~50 lines
  into named helpers. No walls of code.
* **ECS style (EnTT):**
  * Components are plain data. Behavior lives in systems that iterate views (`registry.view<A, B>()`).
  * A type (building, unit, NPC, ship) is one data-table entry plus a factory that attaches its components.
    A new mechanic is a new component plus a system, never a `switch` on the type enum in several files.
  * Validate every stored `entt::entity` with `registry.valid(e)` before use. Never add or remove components of the iterated type while iterating a view; defer it.
  * Cleanup of dependent state (jobs, ships, caravans) hooks `registry.on_destroy<T>()` instead of being repeated in many systems.
  * Shared singletons (e.g. wind) go in `registry.ctx()`, not in globals.
* **Extensibility over switches.** The recipes are in `ARCHITECTURE.md` ("Extending the game"); keep them true.
* **Formatting:** the tree is formatted with `.clang-format`. Run it on every file you touch.
* **Leave every file you touch better than you found it:** the same rules apply to code you edit, not only to new code.

## Definition of done (for every task)

* Both targets build with zero warnings.
* The changed behavior was actually run (scripted server + client run with a screenshot where it is visible); anything only read is listed in `TODO.md`.
* New files follow the code style above; no function over ~50 lines; no multi-line lambdas in containers.
* Style check for files you created or touched: each file starts with the Doxygen file block (`grep -L '@file' <files>` prints nothing) and contains
  no other Doxygen tags (`grep -n '@brief\|@param\|/\*\*' <files>` shows only the header block).
* New env hooks you used are added to `ARCHITECTURE.md`; `TODO.md` is updated; one commit per logical step.
