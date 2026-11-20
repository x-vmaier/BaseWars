# Base Wars - agent handoff

C++23 RTS (EnTT + Raylib + Steamworks). One authoritative `server`, one `client`. Branch: `feature/prompt-implementation`.
Read order: this file, then `TODO.md` (status, roadmap, current task specs), then `ARCHITECTURE.md` (layout, conventions, recipes, tests, hooks).
Specs of tasks that are not yet current live in `BACKLOG.md`; open it only when a task becomes current.

The owner does not approve plans and does not playtest every task. You work autonomously and prove correctness with automated checks.

## Session protocol (one task per session)

1. Take the first Roadmap row in `TODO.md` with status `todo` (currently **T19b**). Skip `blocked` rows.
2. Read the task text (in `TODO.md`, or in `BACKLOG.md` if it is not listed there; move its spec into `TODO.md` first) and the code it names.
   Write a plan of at most 15 lines (design, files, order, the selftest you will add) and continue immediately.
3. Decide small things yourself and log them as one line in the Decisions log (`Txx: decision - reason`).
   **Ask the owner only before irreversible choices** (protocol format, save format, deleting player data) when the task text does not already decide it.
4. Work in small steps. Between steps use only the **fast loop**: an incremental release build (warning-free) and the single relevant `server --selftest <name>`; fix, commit when green.
   Run the full `tools/check.sh` (asan, all selftests, format) **once at the end, before the final commit** - never after every step.
5. Finish with the close-out below, then stop and report.

### Task size

A task should fit one session and one coherent concept. Rough guide: about 12 files and 600 net lines, but **coherence beats the count**:
never split a change into half-working pieces just to hit a number. If the work clearly outgrows the session, stop at a green, committed, shippable state,
split the remainder into new Roadmap rows (same id + suffix) and say so in the report.

### Close-out (short)

* `tools/check.sh` is green.
* `TODO.md`: task moved to Done (one row), next row is current, "only verified by reading" and new Known gaps listed, Decisions added.
* `ARCHITECTURE.md` updated **only if** layout, protocol, save format, config keys, registries, selftests or env hooks changed.
* `CLAUDE.md`: add a rule under "Rules that bit us" only for a genuinely new trap; update the "currently Txx" pointer.
* Commit `Txx: <summary>`. No push, no history rewrite, no force operations.

### If you cannot finish

If the gate cannot go green after reasonable effort: revert to the last green commit (or keep the work on a `wip/Txx` branch), record what failed and why in `TODO.md` Known gaps, and stop.
Also stop for: missing owner-supplied assets or licences (mark the row `blocked: <reason>`, take the next row), or a destructive action outside the repo/build dir (never do it).
A contradiction between two rules is resolved in favour of the Design goals in `TODO.md`; log it. A flaky tool (Steam, window manager) is not a reason to stop: retry.

## Verification

* `tools/check.sh` is the gate: release + asan builds with zero warnings, every `server --selftest`, clang-format check. Docs-sync and style greps are **advisory warnings**, not failures.
* Every task adds at least one selftest scenario that asserts behaviour with numbers (headless, fixed seed, no Steam, fast-forwarded ticks). Selftests also run `World::checkInvariants()`.
  Keep it **concise and focused**: core assertions and invariants only, no bloated boilerplate (reuse `Fixture` helpers, one short function per scenario).
  **Exception:** purely visual, UI, shader or audio tasks need no new C++ selftest; the existing selftests staying green plus a build and (optionally) a `BW_SHOT` check suffice. Say so in the report.
* The safety net itself stays intact: never delete or weaken an existing selftest or invariant to save effort.
* Test unhappy paths where relevant: save/reload mid-action, disconnect/reconnect, destroyed target, empty/full inventory, two players interacting.
* **Stuck-state rule:** every player-facing state (defeat, resettle offer, pending build, queue, convoy, voyage) has a guaranteed exit (timeout, cancel, auto-resolve) proven by a selftest.
* Visible changes (UI, rendering) get one scripted client run with `BW_SHOT` that you view yourself and judge. Screenshots are **optional evidence, never part of the gate** (the client run is flaky).
* Client-heavy tasks (interpolation, prediction, map art, juice) cannot be fully proven headless. Say so in the report and list what the owner should look at in a short manual playtest.
* Never claim a mechanic works unless it was run. Whatever was only read goes into `TODO.md` "only verified by reading".

## Build

* Steamworks SDK must exist in `third_party/steamworks` (git-ignored). CMake fails without it.
* `cmake --preset release && cmake --build build/release`; sanitizer build `cmake --preset asan`. Both targets build with zero warnings.
  After CMake changes re-run configure (sources are globbed).

## Running the game

* Selftests need no Steam : `./server --selftest <name|list|all>`.
* Real client runs need the Steam client running (`steam -silent &`, wait until `~/.steam/steam.pipe` is fresh), otherwise `SteamAPI_Init` fails.
* From the build dir: `./server 7777 42 mysave.bin Test --local &` (fresh save name each time), then
  `BW_SHOT=out.png BW_SHOT_FRAMES=300 timeout 60 ./client --connect 127.0.0.1:7777`, view the PNG. `tools/shot.sh` wraps this with retries.
* Hooks are listed in `ARCHITECTURE.md`. `BW_NETLOG=1` traces Steam connection states.
* Known flakiness: about 1 in 3 scripted client runs time out (retry up to 3 times); the client segfaults in `steamclient.so` at exit (ignore).

## Rules that bit us

* **Identity is the authenticated 64-bit SteamID** (T18a: verified via Steam auth tickets; `--local` accepts dev ids only). Never key ownership, saves or names on machine ids, local files, IPs or connection slots; never trust a client-sent name.
* **Server authority:** the server owns all simulation state. A feature with new state needs: component + system, replication, `SaveSystem` entry (bump `SAVE_MAGIC`), `server.cfg` tunables, an invariant if it can dangle.
* **New content goes through the registries** (`KindDefs.hpp` entry + `KT_*` tag, `KindFactories.cpp`, drawer/art tables). No `switch` or `if (kind == ...)` chains.
* **Protocol:** append-only inside the T19a envelope (`PROTOCOL_VERSION`, command seq, tick-numbered unreliable `S_State` chunks under `SNAPSHOT_MTU`). Frequent state goes into snapshot chunks, never into a big message; one-off events stay reliable.
* **Real time vs sim time:** anything that must survive restarts or offline time stores a wall-clock expiry, never a tick count.
* **Fairness:** a threat the player cannot see coming or counter is a bug. Anything that can destroy progress needs a telegraph and a cap. Threats come only from the Pacing Director (after T25a) or explicit player action.
* **Assets:** only the Tiny Swords Free Pack in `assets/`. The Enemy Pack is not in the repo; never invent sprites or reference missing files. Generated map art (T21a) and sounds (T28a) are the only exceptions.
* Audio must never crash or block: no audio device = silently disabled.
* Screenshot runs: the tiling WM gives a random window size per run; compare before/after only at equal size.
* Do not put `pkill -f './server 7777'` in a shell line that contains that text (it kills its own shell); use `pkill -x server`.
* Parallel agents: split by disjoint files; only one agent at a time touches shared files (`Shared.hpp`, `World.*`, `PacketHandler.*`, `SaveSystem.*`, `ServerConfig.*`). Sub-agents do not run git write commands.
* Tunables go in `ServerConfig` + `config/server.cfg`; shared gate/cost/time rules in `src/common/Progression.hpp`.
* No `.inc` files, no extra executables or bots (selftests live inside `server`, helper scripts in `tools/`).
* Client UI: every resource cost goes through `ui::drawCosts`; use `ui::` screen/mouse helpers inside UI code.
* Windows: never name a variable `near` / `far`. Builds need the MSVC environment (vcvars64); see `ARCHITECTURE.md` Build.
* Saves must be written in a deterministic order (sort hash sets, order entities by uid): selftest `saveload` compares bytes.
* A player can have no Town Hall (defeated, `DefeatSystem`, or homeless): code that picks a player's hall must handle `townHallOf(id) == entt::null`. While `bp[id].defeat.phase != DF_None` the server drops every command except camera, chat and resettle.
* Never index `KINDS`/`KINFO` with a kind read from the network before checking `k < K_Count`.

## Code style

Goal: code a new contributor can read in one pass and extend without touching many files. `.clang-format` is the formatter; run it on touched files.

* **File header:** a short Doxygen block (`@file`, `@brief`) as the first thing in the file, before `#pragma once`. Doxygen is used nowhere else.
* **Comments:** plain `//`, the *why* only (non-obvious rule, invariant, past bug, unit/ownership). Never restate the code.
* **Lambdas in containers** (draw queue, job list, callbacks) stay tiny and just call a named method.
* **Prefer** functions under ~50 lines and files under ~600 lines. These are guidance: split when you touch a file and it improves clarity, do not shuffle code only to hit a number.
* **ECS (EnTT):** components are plain data; behaviour lives in systems iterating views. A type is one table entry plus a factory. Validate stored entities with `registry.valid(e)`.
  Do not add/remove components of the iterated type while iterating a view. Cleanup of dependents uses `registry.on_destroy<T>()`. Singletons go in `registry.ctx()`.
* **Robustness:** validate everything from the network (range, ownership, rate, size). A malformed packet never crashes or corrupts the server. No `assert` for input checks.
* Leave touched files better than you found them.

## Definition of done

* `tools/check.sh` green (zero warnings, all selftests, format).
* The task's "Done when" is met and covered by a selftest or an inspected screenshot.
* Close-out above is complete.
