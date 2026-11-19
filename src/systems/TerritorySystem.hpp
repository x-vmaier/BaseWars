#pragma once
// Functional territory of buildings, town move and building relocation.
#include "server/World.hpp"

struct Systems;

class TerritorySystem {
public:
    TerritorySystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    void evalFunctional();
    void doMoveTown(int id, int tx, int ty);
    void doRelocate(int id, uint32_t uid, int tx, int ty);
    void relocSecond();

private:
    World& world;
    entt::registry& reg;
    Systems& s;
    void migrateTown(int ow, entt::entity nth);
    void finishReloc(entt::entity n);
};
