/**
 * @file CombatSystem.hpp
 * @brief Damage, looting, kills (incl.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class CombatSystem
{
  public:
    CombatSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void damage(entt::entity e, float amt, int attacker, uint32_t by = 0);
    void processKills();
    void defeat(int victim, int killer, IV hall);
    void fight(entt::entity e, Unit &u, Kind k, int ow, Pos &p,
               float dt); // attack the ordered / nearest hostile target

  private:
    // Whoever damages a storage building (Town Hall, Outpost, Storage House) takes resources in proportion to the
    // damage dealt.
    void lootStorage(entt::entity e, float amt, int attacker);
    void raidCargo(const float *cargo, int owner, int killer);
    void killRewards(entt::entity e, Kind k, int owner, int killer, Pos p);
    void townHallFell(int owner, int killer, uint16_t town, IV tile);
    entt::entity pickTarget(entt::entity e, int ow, bool &explicitOrder);
    void strike(entt::entity e, Unit &u, Kind k, int ow, Pos p, entt::entity tg, Pos tp, float dt);
    World &world;
    entt::registry &reg;
    Systems &s;
};
