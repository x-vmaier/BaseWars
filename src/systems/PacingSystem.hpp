/**
 * @file PacingSystem.hpp
 * @brief Pacing Director: per-player stress / relief rhythm (Calm -> Warning -> Siege -> Aftermath), raid timing and
 * size.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class PacingSystem
{
  public:
    PacingSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void second();               // once per second for every player
    void send(int id);           // S_Pacing for the HUD
    int raidersOf(int id) const; // living raiders targeting this player

  private:
    void tick(int id);
    void begin(int id, Pacing &p);
    void startWarning(int id, Pacing &p, int faction, const Town &from);
    void startSiege(int id, Pacing &p);
    void endSiege(int id, Pacing &p);
    double rnd(double lo, double hi);
    double calmSeconds();
    void enter(Pacing &p, uint8_t phase, double seconds);
    int pickFaction(int id, const Town *&from);
    void assetsOf(int id, int &level, int &buildings, int &troops);
    World &world;
    entt::registry &reg;
    Systems &s;
};
