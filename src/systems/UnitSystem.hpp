#pragma once
// Units per tick: behaviour dispatch (wander, firefighting, workers, convoys, raids, hunting, patrols, monks, combat)
// and movement.
#include "server/World.hpp"

struct Systems;

class UnitSystem
{
  public:
    UnitSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void behave(const std::vector<entt::entity> &units, float dt);
    void move(const std::vector<entt::entity> &units, float dt);

  private:
    void healTick(entt::entity e, Unit &u, Kind k, int ow, Pos p, const std::vector<entt::entity> &units, float dt);
    World &world;
    entt::registry &reg;
    Systems &s;
};
