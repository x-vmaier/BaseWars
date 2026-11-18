Great work on the base game! Now let's expand it into a much richer, larger-scale experience. Update the existing codebase (`shared.hpp`, `server.cpp`, and `client.cpp`—skip `CMakeLists.txt` unless dependencies change) to add the following systems while keeping the game compiling, bug-free, and strictly free of placeholders/TODOs.

### Strict Output Rules (CRITICAL)
- **NO PSEUDOCODE OR STUBS:** Implement 100% of every requested feature. Do not leave `// TODO` comments or truncate functions.
- **Token Management:** Keep the implementation clean and concise. Because this update adds significant depth, if you approach your output token limit, finish the current file cleanly and ask me to reply `"continue"` before outputting the remaining file(s).

### 1. World Generation & Infinite Map
- **Endlessly Large Map:** Replace the fixed grid with a deterministic, seed-based infinite chunk/region system generated on-demand around explored areas and players.
- **Macro-Biomes & Large Oceans:** Use low-frequency noise for large, distinct macro-biomes (**Large Oceans, Plains, Forests, Highlands/Mountains, Deserts, Flower Fields, and Snow Biomes**), combined with lightweight local detail variation.
- **Biome-Specific Resources & Mechanics:**
  - Each biome yields specific resources (e.g., Wood in Forests, Stone/Ore in Mountains/Highlands, Gold/Crystals in Deserts, Herbs/Food in Flower Fields/Plains, Ice/Furs in Snow).
  - **Preferred Biome Bonuses:** Resource buildings (Mines, Lumber Camps, etc.) gain significant production bonuses when built on their preferred biome.
  - **Snow Biome Hazard & Campfires:** Units/buildings in the Snow biome suffer cold penalties/attrition unless a **Campfire** is built nearby to provide a warmth aura.

### 2. Naval Gameplay, Outposts & Building GUIs
- **Harbours & Ships:** Players can build a **Harbour** on coastal tiles adjacent to ocean tiles. Selecting/right-clicking a Harbour allows building **Ships** that can traverse oceans, transport/fight, and explore distant continents.
- **Outposts:** Players can build **Outposts** away from their main base to claim territory, extend vision, and act as forward spawn/supply points.
- **Building Context GUIs:** Right-clicking *any* player building opens a clean, interactive Raylib UI panel showing its stats, biome efficiency bonus, and buttons to **Upgrade** the building or **Train/Build units & ships** (e.g., building ships at the Harbour, knights at Barracks/Town Hall).

### 3. Combat, Controls & Defeat Loop
- **Target Selection & Hotkeys:**
  - Players can directly click/select enemy buildings, Town Halls, and enemy soldiers/knights as attack targets.
  - Add dedicated hotkeys for **Move** (e.g., `M` + click) and **Attack** (e.g., `A` + click) in addition to mouse context controls.
- **Defeat & Respawn Cycle:** When a player's Town Hall is destroyed, they are defeated: their old base turns into **burning, smoking ruins**, and the player automatically respawns at a fresh, unexplored clearing elsewhere in the infinite world with a starter plot.

### 4. Visual Polish, VFX & Minimap
- **Hover Outlines:** Highlight/outline buildings, units, ships, and NPCs when hovered with the mouse cursor.
- **Visual Detail & VFX:** Improve tile/entity visual detail (layered shapes, shadows, biome accents). Add particle effects (**smoke plumes, fire/embers, water wakes, combat hits**) and subtle **camera screen shake** on explosions, attacks, and building destruction.
- **Enhanced Minimap:** Render distinct, recognizable icons on the minimap for **Town Halls, Outposts, Harbours, Ships, and Armies**, along with discovered biome colors.

Output the updated `shared.hpp`, `server.cpp`, and `client.cpp` in full.
