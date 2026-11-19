#pragma once
// Damage, looting, kills (incl. Town Hall defeat) and the per-unit attack logic.
#include "server/World.hpp"

struct Systems;

class CombatSystem
{
public:
    CombatSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void damage(entt::entity e, float amt, int attacker, uint32_t by = 0);
    void processKills();
    void defeat(int victim, int killer);
    void fight(entt::entity e, Unit &u, Kind k, int ow, Pos &p, float dt); // attack the ordered / nearest hostile target

private:
    // Whoever damages a storage building (Town Hall, Outpost, Storage House) takes resources in proportion to the damage dealt.
    void lootStorage(entt::entity e, float amt, int attacker);
    World &world;
    entt::registry &reg;
    Systems &s;
};
