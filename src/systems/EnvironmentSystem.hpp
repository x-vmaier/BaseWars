/**
 * @file EnvironmentSystem.hpp
 * @brief Cold attrition in the snow, waking NPC chunks / regions near players, exploration by units and buildings.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class EnvironmentSystem
{
  public:
    EnvironmentSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void coldAndWake(const std::vector<entt::entity> &units, const std::vector<entt::entity> &blds); // once per second
    void explore(const std::vector<entt::entity> &units, const std::vector<entt::entity> &blds);     // every 4th tick

  private:
    void coldAttrition(const std::vector<entt::entity> &all, const std::vector<entt::entity> &blds);
    void wakeNpcs(const std::vector<entt::entity> &all);
    World &world;
    entt::registry &reg;
    Systems &s;
};
