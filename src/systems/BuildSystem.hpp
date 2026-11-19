/**
 * @file BuildSystem.hpp
 * @brief Buildings per tick: self-raising construction (sites without a pawn job), production + worker respawn,
 * defensive fire.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class BuildSystem
{
  public:
    BuildSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    float buildTimeFor(entt::entity e, Kind k) const; // construction time; relocations take much longer
    void onCompleted(entt::entity e);                 // construction finished (reveal, dust, message)
    bool buildingTick(entt::entity e, float dt);      // false when the building must not work (non-functional)
    void tick(const std::vector<entt::entity> &blds, float dt);

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
