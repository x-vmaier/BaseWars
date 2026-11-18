Great work on the core codebase! Now let's expand it into a fully polished, feature-rich multiplayer game by integrating the **"Tiny Swords" Pixel Art Asset Pack** (analyzing and mapping all local sprite sheets and textures from `assets/`), adding an **Expandable Building Menu**, a **Top-Left Resource HUD**, persistent player sessions, physical supply lines, and alliance mechanics.

Update the existing codebase (`shared.hpp`, `server.cpp`, and `client.cpp`—skip `CMakeLists.txt` unless dependencies change) to add the following systems while keeping the game compiling, bug-free, and strictly free of placeholders/TODOs.

### Strict Output Rules (CRITICAL)

- **NO PSEUDOCODE OR STUBS:** Implement 100% of every requested feature. Do not leave `// TODO` comments or truncate functions.
- **Token Management:** Keep the implementation clean and concise. Because this update adds significant depth, if you approach your output token limit, finish the current file cleanly and ask me to reply `"continue"` before outputting the remaining file(s).

### 1. Asset Analysis & Integration ("Tiny Swords" Pack)

Carefully inspect and map textures directly from your local asset directories relative to the executable:

- **Units & Team Colors:** Replace generic units with exact unit types from `assets/Units/` (`Pawn` for resource gathering/building, `Warrior`, `Archer`, `Lancer`, `Monk`) mapped across team color folders (`Blue Units`, `Red Units`, `Yellow Units`, `Purple Units`, `Black Units`). Utilize their sprite sheets and frame animations (`_Idle`, `_Run`, `_Attack`, etc.).
- **Buildings & Terrain:** Use building sprites from `assets/Buildings/` (Town Halls, Outposts, Harbours, Barracks, Mines, Lumber Camps) and terrain/tilesets from `assets/Terrain/` (Decorations, Trees, Rocks, Gold, Meat, Bushes, Clouds, and Water elements).
- **UI & VFX:** Style the building context menus, expandable building panel, health bars, banners, buttons, and cursors using `assets/UI Elements/`. Use frames from `assets/Particle FX/` for smoke plumes, fire/embers, and water wakes.

### 2. UI: Expandable Building Menu & Resource HUD

- **Top-Left Resource HUD:** Render a clean UI header in the top-left corner displaying the player's live resource counts (Wood, Gold, Stone/Ore, Meat/Food) styled with Tiny Swords UI banners/icons.
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
- **Hover Outlines:** Highlight buildings, units, and NPCs with an asset-styled outline when hovered.
- **Defeat & Respawn Cycle:** When a player's Town Hall is destroyed, their base turns into **burning, smoking ruins** using particle effects, and they respawn at a fresh, unexplored clearing elsewhere in the infinite world.

Output the updated `shared.hpp`, `server.cpp`, and `client.cpp` in full.
