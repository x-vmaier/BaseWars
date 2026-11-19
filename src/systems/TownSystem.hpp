#pragma once
// NPC faction towns: founding per region, garrisons, destruction / respawn, regeneration.
#include "server/World.hpp"

struct Systems;

class TownSystem {
public:
    TownSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    bool townSiteOk(int cx, int cy);
    bool footprintFree(Kind k, int tx, int ty);
    void buildTown(Town& t);
    void spawnTownUnit(const Town& t, Kind k, int i);
    void processRegion(int rx, int ry);
    void townDestroyed(uint16_t town, int killer);
    void townTick();

private:
    World& world;
    entt::registry& reg;
    Systems& s;
};
