/**
 * @file RaidSystem.hpp
 * @brief Faction raiding parties against hostile players.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class RaidSystem
{
  public:
    RaidSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void spawnRaid(int player, int f);
    void runRaider(entt::entity e, Unit &u, Raider &rd, float dt);
    void raidTick();

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
