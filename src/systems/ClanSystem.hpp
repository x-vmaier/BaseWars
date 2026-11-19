#pragma once
// Alliances, clans, and players who lost their town (homeless clan members / normal respawn).
#include "server/World.hpp"

struct Systems;

class ClanSystem
{
  public:
    ClanSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void breakAlliance(int a, int b);
    void leaveClan(int id);
    bool baseDefeat(int victim); // true: handled (homeless clan member); false: normal respawn
    void respawnNormally(int id);
    void homelessSecond();
    void homelessBuild(int id, int tx, int ty); // C_Build K_TownHall

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
