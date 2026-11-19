#pragma once
// Binary savegame: mandatory part (profiles, clans, towns, entities) followed by optional appended sections (world, ships, base).
// Bump SAVE_MAGIC whenever the layout changes.
#include "server/World.hpp"

struct Systems;

inline constexpr uint32_t SAVE_MAGIC = 0x37535742u;  // "BWS7" (world + ships + base sections appended)

class SaveSystem {
public:
    SaveSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    void saveGame();
    bool loadGame();

private:
    void worldSave(Writer& w);                   // cleared tiles
    void worldLoad(Reader& r);
    void shipsSave(Writer& w);                   // [u32 nShips]{u32 shipUid, u16 n, u32 passengerUid * n}
    void shipsLoad(Reader& r);
    void saveBase(Writer& w);                    // "BSE1": per-player base state, building / unit base components
    void loadBase(Reader& r);
    World& world;
    entt::registry& reg;
    Systems& s;
};
