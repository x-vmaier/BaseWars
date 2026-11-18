Great work on the core codebase! Now let's expand it into a fully polished, feature-rich multiplayer game by utilizing highly optimized, procedurally generated vector graphics and clean raylib drawing primitives for all visuals, UI, particles, terrain, and units. Add an **Expandable Building Menu**, a **Top-Left Resource HUD**, persistent player sessions, physical supply lines, and alliance mechanics.

Update the existing codebase (`shared.hpp`, `server.cpp`, and `client.cpp`—skip `CMakeLists.txt` unless dependencies change) to add the following systems while keeping the game compiling, bug-free, and strictly free of placeholders/TODOs.

### Strict Output Rules (CRITICAL)

- **NO PSEUDOCODE OR STUBS:** Implement 100% of every requested feature. Do not leave `// TODO` comments or truncate functions.
- **Token Management:** Keep the implementation clean and concise. Because this update adds significant depth, if you approach your output token limit, finish the current file cleanly and ask me to reply `"continue"` before outputting the remaining file(s).

### 1. Procedural Visual Rendering & Polish

Instead of loading external image assets, draw all elements entirely via procedural Raylib rendering primitives (vector shapes, layered gradients, outlines, and geometric icons) designed to look extremely clean, modern, and visually detailed:

- **Units & Team Colors:** Render units procedurally using crisp shapes with distinct team color tinting (Blue, Red, Yellow, Purple, Black) for Pawns, Warriors, Archers, Lancers, and Monks, featuring smooth movement and action states.
- **Buildings & Terrain:** Render buildings (Town Halls, Outposts, Harbours, Barracks, Mines, Lumber Camps) and terrain elements (Trees, Rocks, Gold veins, Bushes, Clouds, and Water) using layered geometric styling, distinct color palettes per biome, and dynamic shadows.
- **UI & VFX:** Style the building context menus, expandable building panel, health bars, banners, buttons, and cursors using clean UI panels and clean typography. Use procedural particle effects for smoke plumes, fire/embers, and water wakes.

### 2. UI: Expandable Building Menu & Resource HUD

- **Top-Left Resource HUD:** Render a clean UI header in the top-left corner displaying the player's live resource counts (Wood, Gold, Stone/Ore, Meat/Food) styled with clean vector icons and panels.
- **Expandable Building Menu:** Implement a collapsible/expandable sidebar or radial building menu where players can select which building to construct (Town Hall, Outpost, Harbour, Barracks, Mine, Lumber Camp, Campfire) before placing it on valid tiles.

### 3. World Generation & Infinite Map

- **Endlessly Large Map:** Replace fixed grids with a deterministic, seed-based infinite chunk/region system generated on-demand around explored areas and players.
- **Macro-Biomes & Large Oceans:** Use low-frequency noise for large macro-biomes (**Large Oceans, Plains, Forests, Highlands/Mountains, Deserts, Flower Fields, and Snow Biomes**), combined with local detail variation.
- **Biome-Specific Resources & Mechanics:**
  - Each biome yields specific resources (e.g., Wood in Forests, Stone/Ore in Mountains/Highlands, Gold in Deserts, Meat in Plains, Ice/Furs in Snow).
  - **Preferred Biome Bonuses:** Resource buildings gain major production bonuses when built on their preferred biome.
  - **Snow Biome Hazard & Campfires:** Units/buildings in the Snow biome suffer cold attrition unless a **Campfire** is built nearby to provide a warmth aura.

### 4. Player Identification, Persistence & Server State

- **Player Profiles:** Assign each player a unique **UUID** and **Player Name** upon connection, stored in network packets and player structs.
- **Server State Persistence:** The server must serialize and persist the complete game state (players, buildings, units, resources, unlocked map chunks) to disk (e.g., `savegame.json` or binary).
- **Seamless Rejoining:** When a player disconnects and rejoins with their UUID, the server loads their exact state, position, base, and resources so they continue where they left off.

### 5. Alliance System & Shared Vision/Resources

- **Alliance Invites:** Players can send alliance invites. When accepted:
  - **Shared Fog of War:** Allied players share discovered land and vision maps.
  - **Shared Economy:** Allied players share mutual economic support access.

### 6. Strict Economy & Supply Line Troops

- **No Farms, No Income:** Resources do not increase passively; players *must* build resource farms (Mines, Lumber Camps) on valid tiles to generate income.
- **Offline Production Penalty:** When a player is offline, resource production drops to a significantly slower rate.
- **Physical Transport Troops (Pawns):**
  - Physical `Pawn` units must travel from resource farms back to a storage/Town Hall building to deliver resources.
  - These transport troops can be intercepted and **attacked** by other players to raid shipments.

### 7. Naval Gameplay, Outposts & Building Context GUIs

- **Harbours & Ships:** Build a **Harbour** on coastal tiles adjacent to oceans. Right-clicking a Harbour opens a menu to build **Ships** that traverse oceans and explore distant continents.
- **Outposts:** Build **Outposts** away from your base to claim territory and act as forward spawn points.
- **Building GUIs:** Right-clicking any player building opens an interactive UI panel showing stats, biome efficiency, and upgrade/train options.

### 8. Combat, Controls, Hotkeys & Defeat Loop

- **Target Selection & Hotkeys:** Directly click/select enemy buildings, Town Halls, and units as attack targets. Add hotkeys for **Move** (`M` + click) and **Attack** (`A` + click).
- **Hover Outlines:** Highlight buildings, units, and NPCs with clean procedural glowing outlines when hovered.
- **Defeat & Respawn Cycle:** When a player's Town Hall is destroyed, their base turns into **burning, smoking ruins** using procedural particle effects, and they respawn at a fresh, unexplored clearing elsewhere in the infinite world.

Output the updated `shared.hpp`, `server.cpp`, and `client.cpp` in full.
