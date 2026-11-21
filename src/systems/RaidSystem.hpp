/**
 * @file RaidSystem.hpp
 * @brief Faction raiding parties against hostile players (when and how strong: PacingSystem).
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class RaidSystem
{
  public:
    RaidSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    // Sends `n` raiders of faction `f` from its nearest town; false if there is none or nobody could be sent.
    bool spawnRaid(int player, int f, int n);
    void runRaider(entt::entity e, Unit &u, Raider &rd, float dt);
    const Town *nearestTown(int f, Pos tp);

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
