/**
 * @file ObjectiveSystem.hpp
 * @brief Objectives: drains the objective event bus, advances each player's short / medium / long chain, grants rewards
 * once and replicates the active goals.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class ObjectiveSystem
{
  public:
    ObjectiveSystem(World &world, Systems &s);

    void second();     // once per second: bus -> counters, then resolve finished / timed-out objectives
    void send(int id); // S_Objectives
    int progressOf(int id, const ObjDef &d) const;

  private:
    void drain();
    void evaluate(int id);
    void finish(int id, int tier, bool rewarded);
    int buildingsOf(int id, int kind) const;
    World &world;
    entt::registry &reg;
    Systems &s;
};
