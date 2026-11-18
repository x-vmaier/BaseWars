Great progress! Now let's implement persistent player sessions, physical economic supply lines, and alliance mechanics. Update the existing codebase (`shared.hpp`, `server.cpp`, and `client.cpp`—skip `CMakeLists.txt` unless dependencies change) to add the following systems while keeping the game compiling, bug-free, and strictly free of placeholders/TODOs.

### Strict Output Rules (CRITICAL)

- **NO PSEUDOCODE OR STUBS:** Implement 100% of every requested feature. Do not leave `// TODO` comments or truncate functions.
- **Token Management:** Keep the implementation clean and concise. Because this update adds significant depth, if you approach your output token limit, finish the current file cleanly and ask me to reply `"continue"` before outputting the remaining file(s).

### 1. Player Identification, Persistence & Server State

- **Player Profiles:** Assign each player a unique **UUID** and **Player Name** upon connection, stored in network packets and player structs.
- **Server State Persistence:** The server must serialize and persist the complete game state (players, buildings, units, resources, unlocked map chunks) to disk (e.g., a `savegame.json` or simple binary save file).
- **Seamless Rejoining:** When a player disconnects and rejoins with their UUID, the server loads their exact state, position, base, and resources so they continue right where they left off.

### 2. Alliance System & Shared Vision/Resources

- **Alliance Invites:** Players can send alliance invites to other players. When accepted:
  - **Shared Fog of War:** Allied players share their discovered land / vision maps.
  - **Shared Economy:** Allied players share resource pools or mutual economic support access.

### 3. Strict Economy & Supply Line Troops

- **No Farms, No Income:** Resources do not increase passively from thin air; a player *must* build appropriate resource farms (e.g., Mines, Lumber Camps) on valid tiles to generate income.
- **Offline Production Penalty:** When a player is offline, resource production drops to a significantly slower rate.
- **Physical Transport Troops (Caravans/Pawns):**
  - Instead of instant teleportation, physical units (using the asset pack's `Pawn` units) must travel from resource farms (like a mine) back to a storage/Town Hall building to deliver the resources.
  - These transport troops can be intercepted, intercepted, and **attacked** by other players to raid their shipments.

Output the updated `shared.hpp`, `server.cpp`, and `client.cpp` in full.
